/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "parakeet_worker.h"

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
    m_modelOk = false;

    const ParakeetDevice dev =
        (device == 1) ? ParakeetDevice::Vulkan : ParakeetDevice::Cpu;

    if (!m_backend.load(dev)) {
        // Fall back to CPU if Vulkan was requested and failed.
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
    Q_EMIT modelReady(true, QString());
}

void ParakeetWorker::beginStream()
{
    if (!m_modelOk) {
        Q_EMIT streamStarted(false, QStringLiteral("model not loaded"));
        return;
    }
    if (!m_backend.streamBegin()) {
        Q_EMIT streamStarted(false, m_backend.lastError());
        return;
    }
    Q_EMIT streamStarted(true, QString());
}

void ParakeetWorker::feedPcm(const QVector<float> &samples)
{
    if (!m_backend.hasStream() || samples.isEmpty()) {
        return;
    }
    const StreamFeedResult r = m_backend.streamFeed(samples.constData(), samples.size());
    if (!r.ok) {
        // Surface as an empty finalize-style error path on the next stop.
        return;
    }
    if (!r.text.isEmpty() || r.eouMask != 0) {
        Q_EMIT textFinalized(r.text, r.eouMask);
    }
}

void ParakeetWorker::finalizeStream()
{
    if (!m_backend.hasStream()) {
        Q_EMIT streamFinished(QString(), QStringLiteral("no active stream"));
        return;
    }
    const StreamFeedResult r = m_backend.streamFinalize();
    m_backend.streamEnd();
    if (!r.ok) {
        Q_EMIT streamFinished(QString(), r.error);
        return;
    }
    Q_EMIT streamFinished(r.text, QString());
}

void ParakeetWorker::cancelStream()
{
    m_backend.streamEnd();
    Q_EMIT streamCancelled();
}

} // namespace kea
