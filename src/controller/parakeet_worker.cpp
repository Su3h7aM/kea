/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "parakeet_worker.h"

#include <vector>

#include "logging.h"

namespace kea {

ParakeetWorker::ParakeetWorker(QObject *parent)
    : QObject(parent)
{
}

ParakeetWorker::~ParakeetWorker()
{
    m_backend.streamEnd();
}

void ParakeetWorker::loadBackend(int device, const QString &modelPath)
{
    m_backend.streamEnd();
    m_pcmBuffer.clear();
    m_sessionActive = false;
    m_offlineMode = false;
    m_modelOk = false;

    const ParakeetDevice dev =
        (device == 1) ? ParakeetDevice::Vulkan : ParakeetDevice::Cpu;

    if (!m_backend.load(dev)) {
        if (dev == ParakeetDevice::Vulkan) {
            if (!m_backend.load(ParakeetDevice::Cpu)) {
                Q_EMIT modelReady(false, m_backend.lastError());
                return;
            }
        } else {
            Q_EMIT modelReady(false, m_backend.lastError());
            return;
        }
    }
    if (!m_backend.loadModel(modelPath)) {
        Q_EMIT modelReady(false, m_backend.lastError());
        return;
    }
    m_modelOk = true;
    qCInfo(keaLog) << "model loaded:" << modelPath;
    Q_EMIT modelReady(true, QString());
}

void ParakeetWorker::unloadBackend()
{
    m_backend.unload();
    m_modelOk = false;
    m_sessionActive = false;
    m_offlineMode = false;
    m_pcmBuffer.clear();
    qCInfo(keaLog) << "backend unloaded";
    Q_EMIT modelUnloaded();
}

void ParakeetWorker::beginSession()
{
    if (!m_modelOk) {
        Q_EMIT sessionStarted(false, QStringLiteral("model not loaded"), false);
        return;
    }
    m_pcmBuffer.clear();
    m_sessionActive = false;
    m_offlineMode = false;

    // Prefer streaming when the model supports it.
    if (m_backend.streamBegin()) {
        m_offlineMode = false;
        m_sessionActive = true;
        qCInfo(keaLog) << "session started (streaming)";
        Q_EMIT sessionStarted(true, QString(), false);
        return;
    }

    // Offline models (TDT/CTC/RNNT) reject stream_begin — buffer + batch instead.
    m_offlineMode = true;
    m_sessionActive = true;
    qCInfo(keaLog) << "session started (offline buffer mode)";
    Q_EMIT sessionStarted(true, QString(), true);
}

void ParakeetWorker::feedPcm(const QList<float> &samples)
{
    if (!m_sessionActive || samples.isEmpty()) {
        return;
    }
    if (m_offlineMode) {
        m_pcmBuffer.append(samples);
        return;
    }
    // Streaming path needs a contiguous buffer.
    const std::vector<float> cont(samples.begin(), samples.end());
    const StreamFeedResult r = m_backend.streamFeed(cont.data(), int(cont.size()));
    if (!r.ok) {
        return;
    }
    if (!r.text.isEmpty() || r.eouMask != 0) {
        Q_EMIT textFinalized(r.text, r.eouMask);
    }
}

void ParakeetWorker::finalizeSession()
{
    if (!m_sessionActive) {
        Q_EMIT sessionFinished(QString(), QStringLiteral("no active session"));
        return;
    }
    m_sessionActive = false;

    if (m_offlineMode) {
        if (m_pcmBuffer.isEmpty()) {
            m_pcmBuffer.clear();
            Q_EMIT sessionFinished(QString(), QString());
            return;
        }
        qCInfo(keaLog) << "offline transcribe" << m_pcmBuffer.size() << "samples @16k";
        // QList may be non-contiguous in theory; copy to vector for C API.
        const std::vector<float> cont(m_pcmBuffer.begin(), m_pcmBuffer.end());
        m_pcmBuffer.clear();
        const TranscriptionResult r =
            m_backend.transcribePcm(cont.data(), int(cont.size()), 16000);
        if (!r.ok) {
            Q_EMIT sessionFinished(QString(), r.text);
            return;
        }
        Q_EMIT sessionFinished(r.text, QString());
        return;
    }

    const StreamFeedResult r = m_backend.streamFinalize();
    m_backend.streamEnd();
    if (!r.ok) {
        Q_EMIT sessionFinished(QString(), r.error);
        return;
    }
    Q_EMIT sessionFinished(r.text, QString());
}

void ParakeetWorker::cancelSession()
{
    m_sessionActive = false;
    m_pcmBuffer.clear();
    m_backend.streamEnd();
    Q_EMIT sessionCancelled();
}

} // namespace kea
