/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * ParakeetWorker — all parakeet stream_* calls live on this object's thread.
 *
 * parakeet's C-API is not thread-safe per context (RFC invariant R7). Move this
 * QObject onto a dedicated QThread and only talk to it via queued signals/slots.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

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
    void beginStream();
    void feedPcm(const QVector<float> &samples);
    void finalizeStream(); // flush tail + free session
    void cancelStream();   // free session without commit

Q_SIGNALS:
    void modelReady(bool ok, const QString &error);
    void streamStarted(bool ok, const QString &error);
    /// Newly finalized text + EOU/EOB mask from one feed.
    void textFinalized(const QString &text, int eouMask);
    /// Final tail after finalizeStream (may be empty).
    void streamFinished(const QString &tail, const QString &error);
    void streamCancelled();

private:
    ParakeetBackend m_backend;
    bool m_modelOk = false;
};

} // namespace kea
