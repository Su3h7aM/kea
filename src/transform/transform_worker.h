/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * TransformWorker — runs TextTransformer::transform on a dedicated thread.
 * DictationController queues work here so ASR + GUI never block on LLM latency.
 */
#pragma once

#include <QObject>
#include <QString>

#include "transform/text_transformer.h"

namespace kea {

class TransformWorker : public QObject
{
    Q_OBJECT

public:
    explicit TransformWorker(TextTransformer *transformer, QObject *parent = nullptr);

    TextTransformer *transformer() const { return m_transformer; }
    void setTransformer(TextTransformer *t) { m_transformer = t; }

public Q_SLOTS:
    void loadModel(const QString &path);
    void unloadModel();
    void transform(const QString &utterance, int styleInt, quint64 requestId);

Q_SIGNALS:
    void modelReady(bool ok, const QString &error);
    void modelUnloaded();
    /// requestId echoes the call so stale results can be dropped.
    void finished(quint64 requestId, const QString &text, const QString &error);

private:
    TextTransformer *m_transformer = nullptr;
};

} // namespace kea
