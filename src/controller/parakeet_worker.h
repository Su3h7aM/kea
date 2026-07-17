/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * ParakeetWorker — all parakeet calls live on this object's thread.
 *
 * Supports:
 *   - Streaming models (realtime EOU): stream_begin / feed / finalize
 *   - Offline models (TDT/CTC/RNNT, e.g. tdt-0.6b-v3): buffer PCM while the
 *     mic is open, then transcribe_pcm once on stop
 *
 * parakeet's C-API is not thread-safe per context (RFC invariant R7).
 */
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "inference/parakeet_backend.h"

namespace kea {

class ParakeetWorker : public QObject
{
    Q_OBJECT

public:
    explicit ParakeetWorker(QObject *parent = nullptr);
    ~ParakeetWorker() override;

public Q_SLOTS:
    void loadBackend(int device /*0=cpu,1=vulkan*/, const QString &modelPath);
    void unloadBackend();
    /// Prefer streaming; if the model is offline-only, buffer PCM instead.
    void beginSession();
    void feedPcm(const QList<float> &samples);
    void finalizeSession(); // stream finalize OR offline transcribe_pcm
    void cancelSession();

Q_SIGNALS:
    void modelReady(bool ok, const QString &error);
    void modelUnloaded();
    /// ok, error, offlineMode (true = buffer+batch; false = live stream)
    void sessionStarted(bool ok, const QString &error, bool offlineMode);
    void textFinalized(const QString &text, int eouMask);
    void sessionFinished(const QString &text, const QString &error);
    void sessionCancelled();

private:
    ParakeetBackend m_backend;
    bool m_modelOk = false;
    bool m_offlineMode = false;
    bool m_sessionActive = false;
    QList<float> m_pcmBuffer;
};

} // namespace kea
