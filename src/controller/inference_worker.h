/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * InferenceWorker — abstract backend worker used by DictationController.
 *
 * Production uses ParakeetWorker (real dlopen backend on a dedicated thread).
 * Unit tests inject a fake that runs on the test thread so the controller
 * state machine (Draining unload deferral, offline vs streaming, cancel)
 * can be exercised without a GGUF, mic, or compositor.
 *
 * All stream_* / load calls for one session must stay on the same thread
 * (parakeet C-API is not thread-safe per context — RFC R7).
 */
#pragma once

#include <QList>
#include <QObject>
#include <QString>

namespace kea {

class InferenceWorker : public QObject
{
    Q_OBJECT

public:
    explicit InferenceWorker(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

public Q_SLOTS:
    virtual void loadBackend(int device /*0=cpu,1=vulkan*/, const QString &modelPath) = 0;
    virtual void unloadBackend() = 0;
    /// Prefer streaming; offline models buffer PCM instead.
    virtual void beginSession() = 0;
    virtual void feedPcm(const QList<float> &samples) = 0;
    virtual void finalizeSession() = 0;
    virtual void cancelSession() = 0;

Q_SIGNALS:
    void modelReady(bool ok, const QString &error);
    void modelUnloaded();
    /// ok, error, offlineMode (true = buffer+batch; false = live stream)
    void sessionStarted(bool ok, const QString &error, bool offlineMode);
    void textFinalized(const QString &text, int eouMask);
    void sessionFinished(const QString &text, const QString &error);
    void sessionCancelled();
};

} // namespace kea
