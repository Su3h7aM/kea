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
#include <QString>

#include "controller/inference_worker.h"
#include "inference/parakeet_backend.h"

namespace kea {

class ParakeetWorker : public InferenceWorker
{
    Q_OBJECT

public:
    explicit ParakeetWorker(QObject *parent = nullptr);
    ~ParakeetWorker() override;

public Q_SLOTS:
    void loadBackend(int device /*0=cpu,1=vulkan*/, const QString &modelPath) override;
    void unloadBackend() override;
    /// Prefer streaming; if the model is offline-only, buffer PCM instead.
    void beginSession() override;
    void feedPcm(const QList<float> &samples) override;
    void finalizeSession() override; // stream finalize OR offline transcribe_pcm
    void cancelSession() override;

private:
    ParakeetBackend m_backend;
    bool m_modelOk = false;
    bool m_offlineMode = false;
    bool m_sessionActive = false;
    QList<float> m_pcmBuffer;
};

} // namespace kea
