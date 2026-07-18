/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "llama_transformer.h"

#include "logging.h"

#include <QRegularExpression>
#include <QtGlobal>

#ifdef KEA_HAS_LLAMA
#include "llama.h"
#include <string>
#include <vector>
#endif

namespace kea {

struct LlamaTransformer::Impl {
#ifdef KEA_HAS_LLAMA
    llama_model *model = nullptr;
    llama_context *ctx = nullptr;
    llama_sampler *sampler = nullptr;
    int nCtx = 2048;
#endif
    QString path;
};

LlamaTransformer::LlamaTransformer(QObject *parent)
    : TextTransformer(parent)
    , m_impl(std::make_unique<Impl>())
{
}

LlamaTransformer::~LlamaTransformer()
{
    unloadModel();
}

bool LlamaTransformer::runtimeAvailable()
{
#ifdef KEA_HAS_LLAMA
    return true;
#else
    return false;
#endif
}

bool LlamaTransformer::isReady() const
{
#ifdef KEA_HAS_LLAMA
    return m_impl && m_impl->model && m_impl->ctx;
#else
    return false;
#endif
}

bool LlamaTransformer::loadModel(const QString &ggufPath)
{
    unloadModel();
#ifndef KEA_HAS_LLAMA
    Q_UNUSED(ggufPath);
    setError(QStringLiteral(
        "llama.cpp was not built into this Kea binary (configure with -DKEA_BUILD_LLAMA=ON)"));
    return false;
#else
    if (ggufPath.isEmpty()) {
        setError(QStringLiteral("empty LLM model path"));
        return false;
    }

    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    // n_gpu_layers=0 → pure CPU for the first cut (matches Kea's default ASR CPU path).
    mparams.n_gpu_layers = 0;

    m_impl->model = llama_model_load_from_file(ggufPath.toUtf8().constData(), mparams);
    if (!m_impl->model) {
        setError(QStringLiteral("llama_model_load_from_file failed for %1").arg(ggufPath));
        llama_backend_free();
        return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = m_impl->nCtx;
    cparams.n_batch = 512;
    m_impl->ctx = llama_init_from_model(m_impl->model, cparams);
    if (!m_impl->ctx) {
        setError(QStringLiteral("llama_init_from_model failed"));
        llama_model_free(m_impl->model);
        m_impl->model = nullptr;
        llama_backend_free();
        return false;
    }

    // Low temperature for deterministic short rewrites.
    auto sparams = llama_sampler_chain_default_params();
    m_impl->sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(m_impl->sampler, llama_sampler_init_temp(0.1f));
    llama_sampler_chain_add(m_impl->sampler, llama_sampler_init_top_k(20));
    llama_sampler_chain_add(m_impl->sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(m_impl->sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    m_impl->path = ggufPath;
    m_lastError.clear();
    qCInfo(keaLog) << "LlamaTransformer loaded" << ggufPath;
    return true;
#endif
}

void LlamaTransformer::unloadModel()
{
#ifdef KEA_HAS_LLAMA
    if (m_impl->sampler) {
        llama_sampler_free(m_impl->sampler);
        m_impl->sampler = nullptr;
    }
    if (m_impl->ctx) {
        llama_free(m_impl->ctx);
        m_impl->ctx = nullptr;
    }
    if (m_impl->model) {
        llama_model_free(m_impl->model);
        m_impl->model = nullptr;
        llama_backend_free();
    }
    m_impl->path.clear();
#endif
}

QString LlamaTransformer::transform(const QString &utterance, TransformStyle style)
{
#ifndef KEA_HAS_LLAMA
    Q_UNUSED(style);
    setError(QStringLiteral("llama.cpp runtime not available"));
    return utterance;
#else
    if (!isReady()) {
        setError(QStringLiteral("LLM model not loaded"));
        return utterance;
    }
    const QString trimmed = utterance.trimmed();
    if (trimmed.isEmpty()) {
        m_lastError.clear();
        return QString();
    }

    const QString system = stylePrompt(style);
    // Keep the user message minimal — small models follow short instructions better.
    const QString user = QStringLiteral(
                             "Fix this speech transcript. Reply with only the fixed text.\n\n%1")
                             .arg(trimmed);

    const std::string systemUtf8 = system.toStdString();
    const std::string userUtf8 = user.toStdString();
    const llama_chat_message messages[] = {
        {"system", systemUtf8.c_str()},
        {"user", userUtf8.c_str()},
    };

    // Prefer the GGUF-embedded chat template (LFM2.5); fall back to a simple
    // ChatML-like string if apply_template fails.
    const char *tmpl = llama_model_chat_template(m_impl->model, /*name=*/nullptr);
    std::string formatted;
    bool usedChatTemplate = false;
    if (tmpl && tmpl[0] != '\0') {
        // First call with null buffer returns required size.
        const int32_t need = llama_chat_apply_template(tmpl, messages, 2, /*add_ass=*/true,
                                                      nullptr, 0);
        if (need > 0) {
            formatted.resize(static_cast<size_t>(need));
            const int32_t wrote = llama_chat_apply_template(tmpl, messages, 2, true,
                                                           formatted.data(), need);
            if (wrote > 0) {
                formatted.resize(static_cast<size_t>(wrote));
                usedChatTemplate = true;
            }
        }
    }
    if (!usedChatTemplate) {
        // Manual ChatML without BOS — tokenizer may add BOS via add_special.
        formatted = "<|im_start|>system\n" + systemUtf8 + "<|im_end|>\n"
                    "<|im_start|>user\n" + userUtf8 + "<|im_end|>\n"
                    "<|im_start|>assistant\n";
    }

    const llama_vocab *vocab = llama_model_get_vocab(m_impl->model);
    // When the chat template already inserted BOS (LFM does), do not add another.
    const bool addSpecial = !usedChatTemplate;
    const int nPrompt = -llama_tokenize(vocab, formatted.c_str(), int(formatted.size()),
                                         nullptr, 0, addSpecial, true);
    if (nPrompt <= 0) {
        setError(QStringLiteral("tokenize failed"));
        return utterance;
    }
    std::vector<llama_token> tokens(static_cast<size_t>(nPrompt));
    if (llama_tokenize(vocab, formatted.c_str(), int(formatted.size()), tokens.data(),
                        int(tokens.size()), addSpecial, true)
        < 0) {
        setError(QStringLiteral("tokenize fill failed"));
        return utterance;
    }

    llama_memory_clear(llama_get_memory(m_impl->ctx), true);
    if (m_impl->sampler) {
        llama_sampler_reset(m_impl->sampler);
    }

    llama_batch batch = llama_batch_get_one(tokens.data(), int(tokens.size()));
    if (llama_decode(m_impl->ctx, batch) != 0) {
        setError(QStringLiteral("llama_decode prompt failed"));
        return utterance;
    }

    // Cap near input length so the model cannot ramble.
    const int maxNew = qBound(48, (trimmed.size() / 2) + 48, 192);
    std::string out;
    for (int i = 0; i < maxNew; ++i) {
        const llama_token id = llama_sampler_sample(m_impl->sampler, m_impl->ctx, -1);
        if (llama_vocab_is_eog(vocab, id)) {
            break;
        }
        char buf[256];
        const int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        if (n > 0) {
            out.append(buf, buf + n);
        }
        llama_batch next = llama_batch_get_one(const_cast<llama_token *>(&id), 1);
        if (llama_decode(m_impl->ctx, next) != 0) {
            setError(QStringLiteral("llama_decode generation failed"));
            break;
        }
        if (out.find("<|im_end|>") != std::string::npos) {
            out = out.substr(0, out.find("<|im_end|>"));
            break;
        }
        if (out.size() > 8) {
            const auto pos = out.find("\n\n");
            if (pos != std::string::npos && pos > 0) {
                out = out.substr(0, pos);
                break;
            }
        }
    }

    const QString rawOut = QString::fromUtf8(out.c_str(), int(out.size())).trimmed();
    qCDebug(keaLog) << "LLM raw out:" << rawOut;
    QString result = sanitizeTransformOutput(rawOut, trimmed);
    if (result.isEmpty()) {
        // Sanitizer was too aggressive or model only emitted control tokens.
        // Prefer raw (tag-stripped) over failing open to the ASR string with an error.
        static const QRegularExpression tags(QStringLiteral(R"(<\|[^>]+\|>)"));
        QString fallback = rawOut;
        fallback.remove(tags);
        fallback = fallback.trimmed();
        if (fallback.isEmpty()) {
            setError(QStringLiteral("empty LLM output — using raw transcript"));
            return utterance;
        }
        result = fallback;
    }
    m_lastError.clear();
    return result;
#endif
}

} // namespace kea
