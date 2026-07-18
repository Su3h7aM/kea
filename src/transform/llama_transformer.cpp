/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "llama_transformer.h"

#include "logging.h"

#include <QRegularExpression>
#include <QtGlobal>

#include <cstdio>

#ifdef KEA_HAS_LLAMA
#include "ggml.h"
#include "llama.h"
#include <string>
#include <vector>
#endif

namespace kea {

namespace {

#ifdef KEA_HAS_LLAMA
/// Drop llama/ggml chatter. Only real warnings/errors.
void llamaLogCallback(enum ggml_log_level level, const char *text, void * /*user*/)
{
    // CONT (5) is used for multi-line continuations of INFO dumps — filter it.
    if (!text || (level != GGML_LOG_LEVEL_WARN && level != GGML_LOG_LEVEL_ERROR)) {
        return;
    }
    QString msg = QString::fromUtf8(text).trimmed();
    if (msg.isEmpty()) {
        return;
    }
    if (level == GGML_LOG_LEVEL_ERROR) {
        qCWarning(keaLog).noquote() << "llama:" << msg;
    } else {
        qCWarning(keaLog).noquote() << "llama:" << msg;
    }
}

void installQuietLlamaLogs()
{
    static bool once = false;
    if (once) {
        return;
    }
    once = true;
    llama_log_set(llamaLogCallback, nullptr);
}
#endif

/// Always-visible compare lines (bypass Qt category filters).
void logAsrLlm(const QString &asr, const QString &llm, const QString &note = {})
{
    std::fprintf(stderr, "[kea] ASR | %s\n", qPrintable(asr));
    if (note.isEmpty()) {
        std::fprintf(stderr, "[kea] LLM | %s\n", qPrintable(llm));
    } else {
        std::fprintf(stderr, "[kea] LLM | %s  (%s)\n", qPrintable(llm), qPrintable(note));
    }
    std::fflush(stderr);
}

} // namespace

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
#ifdef KEA_HAS_LLAMA
    installQuietLlamaLogs();
#endif
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
        "llama.cpp was not built into this Kea binary"));
    return false;
#else
    if (ggufPath.isEmpty()) {
        setError(QStringLiteral("empty LLM model path"));
        return false;
    }

    installQuietLlamaLogs();
    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
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

    auto sparams = llama_sampler_chain_default_params();
    m_impl->sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(m_impl->sampler, llama_sampler_init_temp(0.0f));
    llama_sampler_chain_add(m_impl->sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    m_impl->path = ggufPath;
    m_lastError.clear();
    qCInfo(keaLog) << "LLM loaded" << ggufPath;
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
    logAsrLlm(utterance, utterance, QStringLiteral("no llama runtime"));
    return utterance;
#else
    if (!isReady()) {
        setError(QStringLiteral("LLM model not loaded"));
        logAsrLlm(utterance, utterance, QStringLiteral("not loaded"));
        return utterance;
    }
    const QString trimmed = utterance.trimmed();
    if (trimmed.isEmpty()) {
        m_lastError.clear();
        return QString();
    }

    const QString system = stylePrompt(style);
    // Few-shot format works much better on 230M than long instruction prose.
    // The model must copy the task pattern: INPUT → cleaned OUTPUT only.
    const QString user = QStringLiteral(
                             "You edit speech-to-text. Never answer questions. "
                             "Never chat. Output only the edited line.\n"
                             "\n"
                             "INPUT: helo wrld how are yu\n"
                             "OUTPUT: hello world how are you\n"
                             "\n"
                             "INPUT: um i think we should go now right\n"
                             "OUTPUT: I think we should go now, right?\n"
                             "\n"
                             "INPUT: %1\n"
                             "OUTPUT:")
                             .arg(trimmed);

    // Manual LFM2.5 / ChatML layout. Do NOT use llama_chat_apply_template for
    // LFM: it only supports a fixed template list and mishandles LFM's jinja,
    // which produced garbage generations.
    const std::string systemUtf8 = system.toStdString();
    const std::string userUtf8 = user.toStdString();
    const std::string formatted =
        std::string("<|startoftext|><|im_start|>system\n") + systemUtf8
        + "<|im_end|>\n"
          "<|im_start|>user\n"
        + userUtf8 + "<|im_end|>\n"
                     "<|im_start|>assistant\n";

    const llama_vocab *vocab = llama_model_get_vocab(m_impl->model);
    // Prompt already includes <|startoftext|> (BOS) — do not add another.
    const bool addSpecial = false;
    const int nPrompt = -llama_tokenize(vocab, formatted.c_str(), int(formatted.size()),
                                         nullptr, 0, addSpecial, true);
    if (nPrompt <= 0) {
        setError(QStringLiteral("tokenize failed"));
        logAsrLlm(trimmed, trimmed, QStringLiteral("tokenize failed"));
        return utterance;
    }
    std::vector<llama_token> tokens(static_cast<size_t>(nPrompt));
    if (llama_tokenize(vocab, formatted.c_str(), int(formatted.size()), tokens.data(),
                        int(tokens.size()), addSpecial, true)
        < 0) {
        setError(QStringLiteral("tokenize fill failed"));
        logAsrLlm(trimmed, trimmed, QStringLiteral("tokenize failed"));
        return utterance;
    }

    llama_memory_clear(llama_get_memory(m_impl->ctx), true);
    if (m_impl->sampler) {
        llama_sampler_reset(m_impl->sampler);
    }

    llama_batch batch = llama_batch_get_one(tokens.data(), int(tokens.size()));
    if (llama_decode(m_impl->ctx, batch) != 0) {
        setError(QStringLiteral("llama_decode prompt failed"));
        logAsrLlm(trimmed, trimmed, QStringLiteral("decode failed"));
        return utterance;
    }

    const int maxNew = qBound(24, int(trimmed.size() * 1.5) + 16, 96);
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
        // Stop at first newline (we asked for one line).
        if (out.size() > 2) {
            const auto pos = out.find('\n');
            if (pos != std::string::npos) {
                out = out.substr(0, pos);
                break;
            }
        }
    }

    const QString modelOut = QString::fromUtf8(out.c_str(), int(out.size())).trimmed();
    QString result = sanitizeTransformOutput(modelOut, trimmed);
    if (result.isEmpty()) {
        static const QRegularExpression tags(QStringLiteral(R"(<\|[^>]+\|>)"));
        QString fallback = modelOut;
        fallback.remove(tags);
        fallback = fallback.trimmed();
        if (fallback.isEmpty()) {
            setError(QStringLiteral("empty LLM output — using raw transcript"));
            logAsrLlm(trimmed, trimmed, QStringLiteral("empty model output"));
            return utterance;
        }
        result = fallback;
    }

    // Reject obvious non-edits: model echoed a marker word or the instruction.
    const QString lower = result.toLower();
    if (lower == QStringLiteral("transcription")
        || lower == QStringLiteral("transcript")
        || lower == QStringLiteral("output")
        || lower.startsWith(QStringLiteral("input:"))
        || lower.startsWith(QStringLiteral("output:"))) {
        setError(QStringLiteral("LLM returned garbage — using raw transcript"));
        logAsrLlm(trimmed, result, QStringLiteral("rejected garbage"));
        return utterance;
    }

    m_lastError.clear();
    logAsrLlm(trimmed, result);
    return result;
#endif
}

} // namespace kea
