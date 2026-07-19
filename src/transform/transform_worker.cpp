/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "transform_worker.h"

#include "logging.h"

#include <cstdio>

namespace kea {

TransformWorker::TransformWorker(TextTransformer *transformer, QObject *parent)
    : QObject(parent)
    , m_transformer(transformer)
{
}

void TransformWorker::loadModel(const QString &path)
{
    if (!m_transformer) {
        Q_EMIT modelReady(false, QStringLiteral("no transformer"));
        return;
    }
    if (path.isEmpty()) {
        m_transformer->unloadModel();
        Q_EMIT modelReady(false, QStringLiteral("empty model path"));
        return;
    }
    const bool ok = m_transformer->loadModel(path);
    if (!ok) {
        std::fprintf(stderr, "[kea] LLM load failed: %s\n",
                     qPrintable(m_transformer->lastError()));
        std::fflush(stderr);
        Q_EMIT modelReady(false, m_transformer->lastError().isEmpty()
                                     ? QStringLiteral("LLM model load failed")
                                     : m_transformer->lastError());
        return;
    }
    std::fprintf(stderr, "[kea] LLM ready: %s\n", qPrintable(path));
    std::fflush(stderr);
    Q_EMIT modelReady(true, QString());
}

void TransformWorker::unloadModel()
{
    if (m_transformer) {
        m_transformer->unloadModel();
    }
    Q_EMIT modelUnloaded();
}

void TransformWorker::transform(const QString &utterance, int styleInt, quint64 requestId)
{
    if (!m_transformer) {
        std::fprintf(stderr, "[kea] ASR | %s\n", qPrintable(utterance));
        std::fprintf(stderr, "[kea] LLM | (no transformer)\n");
        std::fflush(stderr);
        Q_EMIT finished(requestId, utterance, QStringLiteral("no transformer"));
        return;
    }
    if (utterance.trimmed().isEmpty()) {
        Q_EMIT finished(requestId, QString(), QString());
        return;
    }
    if (!m_transformer->isReady()) {
        std::fprintf(stderr, "[kea] ASR | %s\n", qPrintable(utterance));
        std::fprintf(stderr, "[kea] LLM | (not ready — using ASR)\n");
        std::fflush(stderr);
        Q_EMIT finished(requestId, utterance,
                        QStringLiteral("LLM not ready — using raw transcript"));
        return;
    }
    const TransformStyle style = TextTransformer::styleFromInt(styleInt);
    // LlamaTransformer::transform also prints [kea] ASR/LLM lines.
    const QString out = m_transformer->transform(utterance, style);
    const QString err = m_transformer->lastError();
    Q_UNUSED(style);
    Q_EMIT finished(requestId, out, err);
}

} // namespace kea
