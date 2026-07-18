/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "transform_worker.h"

#include "logging.h"

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
        Q_EMIT modelReady(false, m_transformer->lastError().isEmpty()
                                     ? QStringLiteral("LLM model load failed")
                                     : m_transformer->lastError());
        return;
    }
    qCInfo(keaLog) << "LLM ready" << path;
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
        Q_EMIT finished(requestId, utterance, QStringLiteral("no transformer"));
        return;
    }
    if (utterance.trimmed().isEmpty()) {
        Q_EMIT finished(requestId, QString(), QString());
        return;
    }
    if (!m_transformer->isReady()) {
        qCWarning(keaLog) << "ASR:" << utterance;
        qCWarning(keaLog) << "LLM: (not ready — using ASR)";
        Q_EMIT finished(requestId, utterance,
                        QStringLiteral("LLM not ready — using raw transcript"));
        return;
    }
    const TransformStyle style = TextTransformer::styleFromInt(styleInt);
    const QString out = m_transformer->transform(utterance, style);
    const QString err = m_transformer->lastError();
    // Always log both sides so we can compare without digging llama spam.
    qCInfo(keaLog).noquote() << "ASR: " << utterance;
    qCInfo(keaLog).noquote() << "LLM: " << out
                             << (err.isEmpty()
                                     ? QString()
                                     : QStringLiteral("  [") + err + QLatin1Char(']'));
    qCInfo(keaLog) << "style=" << TextTransformer::styleName(style);
    Q_EMIT finished(requestId, out, err);
}

} // namespace kea
