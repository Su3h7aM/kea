/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "dictation_controller.h"

#include <QMetaObject>
#include <QVector>

#include "app/app_settings.h"
#include "audio/audio_recorder.h"
#include "controller/parakeet_worker.h"
#include "hotkey/global_hotkey.h"
#include "insert/text_committer.h"

namespace kea {

DictationController::DictationController(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_recorder(std::make_unique<AudioRecorder>(this))
    , m_worker(new ParakeetWorker)
{
    m_statusText = QStringLiteral("Idle");

    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_workerThread.start();

    connect(m_recorder.get(), &AudioRecorder::pcmBlock,
            this, &DictationController::onPcmBlock);
    connect(m_recorder.get(), &AudioRecorder::levelChanged,
            this, &DictationController::onLevel);

    connect(m_worker, &ParakeetWorker::modelReady,
            this, &DictationController::onModelReady);
    connect(m_worker, &ParakeetWorker::streamStarted,
            this, &DictationController::onStreamStarted);
    connect(m_worker, &ParakeetWorker::textFinalized,
            this, &DictationController::onTextFinalized);
    connect(m_worker, &ParakeetWorker::streamFinished,
            this, &DictationController::onStreamFinished);
    connect(m_worker, &ParakeetWorker::streamCancelled,
            this, &DictationController::onStreamCancelled);

    if (m_settings) {
        connect(m_settings, &AppSettings::modelPathChanged, this, [this]() {
            m_modelLoaded = false;
            Q_EMIT modelLoadedChanged();
        });
        connect(m_settings, &AppSettings::backendChanged, this, [this]() {
            m_modelLoaded = false;
            Q_EMIT modelLoadedChanged();
        });
    }
}

DictationController::~DictationController()
{
    cancel();
    m_workerThread.quit();
    m_workerThread.wait(3000);
}

void DictationController::setTextCommitter(TextCommitter *committer)
{
    m_committer = committer;
}

void DictationController::setHotkey(GlobalHotkey *hotkey)
{
    if (m_hotkey) {
        disconnect(m_hotkey, nullptr, this, nullptr);
    }
    m_hotkey = hotkey;
    if (m_hotkey) {
        connect(m_hotkey, &GlobalHotkey::activeChanged,
                this, &DictationController::onHotkeyActive);
        // Discrete trigger acts as toggle when hold events aren't available.
        connect(m_hotkey, &GlobalHotkey::triggered, this, [this]() {
            if (m_state == State::Listening) {
                stop();
            } else if (m_state == State::Idle || m_state == State::Error) {
                start();
            }
        });
    }
}

QString DictationController::stateName() const
{
    switch (m_state) {
    case State::Idle:
        return QStringLiteral("Idle");
    case State::LoadingModel:
        return QStringLiteral("Loading model");
    case State::Listening:
        return QStringLiteral("Listening");
    case State::Draining:
        return QStringLiteral("Finishing");
    case State::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

void DictationController::setState(State s)
{
    if (m_state == s) {
        return;
    }
    m_state = s;
    Q_EMIT stateChanged();
}

void DictationController::setStatus(const QString &text)
{
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    Q_EMIT statusTextChanged();
}

void DictationController::setError(const QString &err)
{
    m_lastError = err;
    Q_EMIT lastErrorChanged();
    if (!err.isEmpty()) {
        setState(State::Error);
        setStatus(QStringLiteral("Error: ") + err);
    }
}

void DictationController::loadModel()
{
    if (!m_settings) {
        setError(QStringLiteral("no settings"));
        return;
    }
    if (m_modelLoadPending) {
        return;
    }
    m_modelLoadPending = true;
    setState(State::LoadingModel);
    setStatus(QStringLiteral("Loading model…"));
    QMetaObject::invokeMethod(m_worker, "loadBackend", Qt::QueuedConnection,
                              Q_ARG(int, m_settings->backend()),
                              Q_ARG(QString, m_settings->modelPath()));
}

void DictationController::onModelReady(bool ok, const QString &error)
{
    m_modelLoadPending = false;
    m_modelLoaded = ok;
    Q_EMIT modelLoadedChanged();
    if (!ok) {
        m_startAfterLoad = false;
        setError(error.isEmpty() ? QStringLiteral("model load failed") : error);
        return;
    }
    setError(QString());
    setState(State::Idle);
    setStatus(QStringLiteral("Model ready"));
    if (m_startAfterLoad) {
        m_startAfterLoad = false;
        beginListening();
    }
}

void DictationController::start()
{
    if (m_state == State::Listening || m_state == State::Draining || m_state == State::LoadingModel) {
        return;
    }
    if (!m_modelLoaded) {
        m_startAfterLoad = true;
        loadModel();
        return;
    }
    beginListening();
}

void DictationController::beginListening()
{
    setError(QString());
    setStatus(QStringLiteral("Starting…"));
    QMetaObject::invokeMethod(m_worker, "beginStream", Qt::QueuedConnection);
}

void DictationController::onStreamStarted(bool ok, const QString &error)
{
    if (!ok) {
        setError(error.isEmpty() ? QStringLiteral("stream begin failed") : error);
        return;
    }
    if (!m_recorder->start()) {
        QMetaObject::invokeMethod(m_worker, "cancelStream", Qt::QueuedConnection);
        setError(QStringLiteral("microphone open failed"));
        return;
    }
    setState(State::Listening);
    setStatus(QStringLiteral("Listening…"));
}

void DictationController::onPcmBlock(const QVector<float> &samples)
{
    if (m_state != State::Listening || samples.isEmpty()) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "feedPcm", Qt::QueuedConnection,
                              Q_ARG(QVector<float>, samples));
}

void DictationController::onLevel(float level)
{
    m_level = level;
    Q_EMIT levelChanged();
}

void DictationController::onTextFinalized(const QString &text, int /*eouMask*/)
{
    if (text.isEmpty()) {
        return;
    }
    if (m_committer) {
        m_committer->commitText(text);
    }
}

void DictationController::stop()
{
    if (m_state != State::Listening) {
        return;
    }
    stopCaptureOnly();
    setState(State::Draining);
    setStatus(QStringLiteral("Finishing…"));
    QMetaObject::invokeMethod(m_worker, "finalizeStream", Qt::QueuedConnection);
}

void DictationController::stopCaptureOnly()
{
    if (m_recorder->isActive()) {
        m_recorder->stop();
    }
    m_level = 0.0f;
    Q_EMIT levelChanged();
}

void DictationController::onStreamFinished(const QString &tail, const QString &error)
{
    if (!error.isEmpty() && tail.isEmpty()) {
        setError(error);
        return;
    }
    if (!tail.isEmpty() && m_committer) {
        m_committer->commitText(tail);
    }
    if (m_committer) {
        m_committer->clearPreedit();
    }
    setState(State::Idle);
    setStatus(QStringLiteral("Idle"));
    setError(QString());
}

void DictationController::cancel()
{
    if (m_state == State::Idle) {
        return;
    }
    stopCaptureOnly();
    m_startAfterLoad = false;
    if (m_committer) {
        m_committer->clearPreedit();
    }
    QMetaObject::invokeMethod(m_worker, "cancelStream", Qt::QueuedConnection);
    setState(State::Idle);
    setStatus(QStringLiteral("Cancelled"));
}

void DictationController::onStreamCancelled()
{
    if (m_state != State::Idle) {
        setState(State::Idle);
        setStatus(QStringLiteral("Idle"));
    }
}

void DictationController::onHotkeyActive(bool active)
{
    if (active) {
        start();
    } else {
        // Release ends the utterance (push-to-talk).
        if (m_state == State::Listening) {
            stop();
        }
    }
}

} // namespace kea
