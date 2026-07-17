/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "dictation_controller.h"

#include <QList>
#include <QMetaObject>
#include <QMetaType>

#include "app/app_settings.h"
#include "audio/audio_recorder.h"
#include "controller/parakeet_worker.h"
#include "hotkey/global_hotkey.h"
#include "insert/text_committer.h"
#include "logging.h"

namespace kea {

DictationController::DictationController(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_recorder(std::make_unique<AudioRecorder>(this))
    , m_worker(new ParakeetWorker)
{
    qRegisterMetaType<QList<float>>("QList<float>");

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
    connect(m_worker, &ParakeetWorker::modelUnloaded,
            this, &DictationController::onModelUnloaded);
    connect(m_worker, &ParakeetWorker::sessionStarted,
            this, &DictationController::onSessionStarted);
    connect(m_worker, &ParakeetWorker::textFinalized,
            this, &DictationController::onTextFinalized);
    connect(m_worker, &ParakeetWorker::sessionFinished,
            this, &DictationController::onSessionFinished);
    connect(m_worker, &ParakeetWorker::sessionCancelled,
            this, &DictationController::onSessionCancelled);

    if (m_settings) {
        connect(m_settings, &AppSettings::modelPathChanged, this, [this]() {
            m_modelLoaded = false;
            Q_EMIT modelLoadedChanged();
        });
        connect(m_settings, &AppSettings::backendChanged, this, [this]() {
            m_modelLoaded = false;
            Q_EMIT modelLoadedChanged();
        });
        connect(m_settings, &AppSettings::activationModeChanged, this, [this]() {
            setActivationMode(m_settings->activationMode());
        });
        // Read the initial activation mode.
        m_activationMode = (m_settings->activationMode() == 1)
                            ? ActivationMode::Toggle
                            : ActivationMode::PushToTalk;
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
        if (m_activationMode == ActivationMode::Toggle) {
            // Toggle: each discrete press flips start/stop.
            connect(m_hotkey, &GlobalHotkey::triggered,
                    this, &DictationController::onHotkeyTriggered);
        } else {
            // Push-to-talk: press = start, release = stop.
            connect(m_hotkey, &GlobalHotkey::activeChanged,
                    this, &DictationController::onHotkeyActive);
        }
    }
}

void DictationController::setActivationMode(int mode)
{
    const auto newMode = (mode == 1) ? ActivationMode::Toggle : ActivationMode::PushToTalk;
    if (newMode == m_activationMode) {
        return;
    }
    m_activationMode = newMode;
    // Re-wire the hotkey connections.
    if (m_hotkey) {
        setHotkey(m_hotkey);
    }
    Q_EMIT activationModeChanged();
}

bool DictationController::isBusy() const
{
    return m_state == State::LoadingModel
        || m_state == State::Starting
        || m_state == State::Listening
        || m_state == State::Draining;
}

QString DictationController::stateName() const
{
    switch (m_state) {
    case State::Idle:
        return QStringLiteral("Idle");
    case State::LoadingModel:
        return QStringLiteral("Loading model");
    case State::Starting:
        return QStringLiteral("Starting");
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

void DictationController::unloadModel()
{
    if (m_state == State::Draining) {
        // A transcription is still in flight on the worker thread (offline
        // transcribe_pcm or stream_finalize). Unloading the backend now would
        // race with that call and drop the result. Defer: onSessionFinished()
        // re-invokes unloadModel() once the session actually resolves.
        m_unloadAfterDrain = true;
        setStatus(QStringLiteral("Finishing… (will unload when done)"));
        qCInfo(keaLog) << "unloadModel deferred until Draining session resolves";
        return;
    }

    // Don't unload while actively dictating.
    if (m_state == State::Listening || m_state == State::Starting) {
        cancel();
    }
    teardownBackend();
    setState(State::Idle);
    setStatus(QStringLiteral("Idle"));
}

void DictationController::teardownBackend()
{
    m_modelLoaded = false;
    m_modelLoadPending = false;
    Q_EMIT modelLoadedChanged();
    qCInfo(keaLog) << "unloading model";
    QMetaObject::invokeMethod(m_worker, "unloadBackend", Qt::QueuedConnection);
}

void DictationController::maybeUnloadAfterDrain()
{
    if (!m_unloadAfterDrain) {
        return;
    }
    m_unloadAfterDrain = false;
    teardownBackend();
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
    m_lastError.clear();
    Q_EMIT lastErrorChanged();
    setState(State::Idle);
    setStatus(QStringLiteral("Ready — hold hotkey to dictate"));
    if (m_startAfterLoad) {
        m_startAfterLoad = false;
        beginListening();
    }
}

void DictationController::onModelUnloaded()
{
    // Worker confirmed the backend is torn down. A deferred teardown may
    // intentionally preserve a terminal Error or Cancelled status.
    if (m_state == State::LoadingModel) {
        setState(State::Idle);
        setStatus(QStringLiteral("Idle"));
    }
}

void DictationController::start()
{
    if (m_unloadAfterDrain) {
        qCDebug(keaLog) << "start ignored while deferred unload is pending";
        return;
    }
    // Re-entrancy guard: while Starting/Listening/Draining, ignore extra starts.
    if (isBusy() && m_state != State::LoadingModel) {
        qCDebug(keaLog) << "start ignored, state=" << stateName();
        return;
    }
    if (m_state == State::LoadingModel) {
        m_startAfterLoad = true;
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
    // Synchronous transition so a second start() before the worker answers is ignored.
    m_stopWhenStarted = false;
    setError(QString());
    setState(State::Starting);
    setStatus(QStringLiteral("Starting…"));
    qCInfo(keaLog) << "beginListening";
    QMetaObject::invokeMethod(m_worker, "beginSession", Qt::QueuedConnection);
}

void DictationController::onSessionStarted(bool ok, const QString &error, bool offlineMode)
{
    // Stale reply: user already cancelled/stopped.
    if (m_state != State::Starting) {
        qCDebug(keaLog) << "ignoring sessionStarted in state" << stateName();
        if (ok) {
            QMetaObject::invokeMethod(m_worker, "cancelSession", Qt::QueuedConnection);
        }
        return;
    }

    if (!ok) {
        setError(error.isEmpty() ? QStringLiteral("session start failed") : error);
        return;
    }

    m_offlineMode = offlineMode;

    // Hotkey released while we were still Starting — capture nothing, just clean up.
    if (m_stopWhenStarted) {
        m_stopWhenStarted = false;
        QMetaObject::invokeMethod(m_worker, "cancelSession", Qt::QueuedConnection);
        setState(State::Idle);
        setStatus(QStringLiteral("Idle"));
        return;
    }

    if (!m_recorder->start()) {
        QMetaObject::invokeMethod(m_worker, "cancelSession", Qt::QueuedConnection);
        const QString detail = m_recorder->lastError();
        setError(detail.isEmpty() ? QStringLiteral("microphone open failed")
                                  : QStringLiteral("microphone open failed: ") + detail);
        return;
    }

    setState(State::Listening);
    setStatus(offlineMode ? QStringLiteral("Listening (offline)…")
                          : QStringLiteral("Listening…"));
    qCInfo(keaLog) << "listening" << (offlineMode ? "offline" : "streaming");
}

void DictationController::onPcmBlock(const QList<float> &samples)
{
    if (m_state != State::Listening || samples.isEmpty()) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "feedPcm", Qt::QueuedConnection,
                              Q_ARG(QList<float>, samples));
}

void DictationController::onLevel(float level)
{
    if (m_state != State::Listening) {
        return;
    }
    m_level = level;
    Q_EMIT levelChanged();
}

void DictationController::onTextFinalized(const QString &text, int /*eouMask*/)
{
    if (text.isEmpty() || m_state == State::Idle) {
        return;
    }
    if (m_committer) {
        m_committer->commitText(text);
    }
}

void DictationController::stop()
{
    if (m_state == State::Starting) {
        // Defer stop until session actually starts (or just mark cancel).
        m_stopWhenStarted = true;
        setStatus(QStringLiteral("Cancelling…"));
        return;
    }
    if (m_state != State::Listening) {
        return;
    }
    stopCaptureOnly();
    setState(State::Draining);
    setStatus(m_offlineMode ? QStringLiteral("Transcribing…") : QStringLiteral("Finishing…"));
    qCInfo(keaLog) << "stop → finalize";
    QMetaObject::invokeMethod(m_worker, "finalizeSession", Qt::QueuedConnection);
}

void DictationController::stopCaptureOnly()
{
    if (m_recorder->isActive()) {
        m_recorder->stop();
    }
    m_level = 0.0f;
    Q_EMIT levelChanged();
}

void DictationController::onSessionFinished(const QString &text, const QString &error)
{
    // Ignore stale finishes if we already moved on.
    if (m_state != State::Draining && m_state != State::Listening) {
        qCDebug(keaLog) << "ignoring sessionFinished in state" << stateName();
        return;
    }

    stopCaptureOnly();

    if (!error.isEmpty() && text.isEmpty()) {
        setError(error);
        maybeUnloadAfterDrain();
        return;
    }

    // Return to Idle *before* inserting text so a new hotkey press is allowed,
    // and so commit happens outside the worker-reply stack.
    m_lastError.clear();
    Q_EMIT lastErrorChanged();
    setState(State::Idle);
    setStatus(QStringLiteral("Idle"));

    if (!text.isEmpty()) {
        qCInfo(keaLog) << "transcript:" << text;
        if (m_committer) {
            // Queued so Wayland commit runs on a clean event-loop turn.
            const QString copy = text;
            QMetaObject::invokeMethod(
                this,
                [this, copy]() {
                    if (m_committer) {
                        m_committer->commitText(copy);
                        m_committer->clearPreedit();
                    }
                },
                Qt::QueuedConnection);
        }
    } else {
        qCInfo(keaLog) << "transcript empty";
        if (m_committer) {
            m_committer->clearPreedit();
        }
    }

    maybeUnloadAfterDrain();
}

void DictationController::cancel()
{
    if (m_state == State::Idle) {
        return;
    }
    m_stopWhenStarted = false;
    m_startAfterLoad = false;
    stopCaptureOnly();
    if (m_committer) {
        m_committer->clearPreedit();
    }
    QMetaObject::invokeMethod(m_worker, "cancelSession", Qt::QueuedConnection);
    setState(State::Idle);
    setStatus(QStringLiteral("Cancelled"));
}

void DictationController::onSessionCancelled()
{
    stopCaptureOnly();
    maybeUnloadAfterDrain();
    if (m_state != State::Idle && m_state != State::Error) {
        setState(State::Idle);
        setStatus(QStringLiteral("Idle"));
    }
}

void DictationController::onHotkeyActive(bool active)
{
    qCDebug(keaLog) << "hotkey active=" << active << "state=" << stateName();
    if (active) {
        start();
    } else {
        // Release ends the utterance (push-to-talk).
        stop();
    }
}

void DictationController::onHotkeyTriggered()
{
    qCDebug(keaLog) << "hotkey triggered (toggle) state=" << stateName();
    if (isBusy()) {
        stop();
    } else {
        start();
    }
}

} // namespace kea
