/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "dictation_controller.h"

#include <QFileInfo>
#include <QList>
#include <QMetaObject>
#include <QMetaType>
#include <QThread>

#include "app/app_settings.h"
#include "audio/audio_recorder.h"
#include "controller/inference_worker.h"
#include "controller/parakeet_worker.h"
#include "hotkey/global_hotkey.h"
#include "insert/insertion_router.h"
#include "logging.h"
#include "transform/llama_transformer.h"
#include "transform/text_transformer.h"
#include "transform/transform_worker.h"

namespace kea {

void DictationController::initCommon(AppSettings *settings)
{
    qRegisterMetaType<QList<float>>("QList<float>");
    m_settings = settings;
    m_statusText = QStringLiteral("Idle");

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
        connect(m_settings, &AppSettings::postProcessEnabledChanged, this, [this]() {
            if (postProcessActive()) {
                ensureTransformModelLoaded();
            }
        });
        connect(m_settings, &AppSettings::llmModelPathChanged, this, [this]() {
            m_transformModelReady = false;
            if (postProcessActive()) {
                ensureTransformModelLoaded();
            }
        });
        m_activationMode = (m_settings->activationMode() == 1)
                               ? ActivationMode::Toggle
                               : ActivationMode::PushToTalk;
    }
}

void DictationController::setupTransform(TextTransformer *externalTransformer)
{
    if (externalTransformer) {
        m_transformer = externalTransformer;
        m_ownTransformer = false;
        m_transformWorker = new TransformWorker(m_transformer);
        // Same-thread for tests.
        connect(m_transformWorker, &TransformWorker::finished,
                this, &DictationController::onTransformFinished);
        connect(m_transformWorker, &TransformWorker::modelReady,
                this, &DictationController::onTransformModelReady);
        return;
    }

    m_ownTransformer = true;
    m_transformer = new LlamaTransformer;
    m_transformWorker = new TransformWorker(m_transformer);
    // Both live on the transform thread — llama.cpp is not thread-safe across
    // contexts if we call load/transform from different threads.
    m_transformer->moveToThread(&m_transformThread);
    m_transformWorker->moveToThread(&m_transformThread);
    m_transformThread.start();
    connect(m_transformWorker, &TransformWorker::finished,
            this, &DictationController::onTransformFinished);
    connect(m_transformWorker, &TransformWorker::modelReady,
            this, &DictationController::onTransformModelReady);
}

void DictationController::wireWorker()
{
    connect(m_worker, &InferenceWorker::modelReady,
            this, &DictationController::onModelReady);
    connect(m_worker, &InferenceWorker::modelUnloaded,
            this, &DictationController::onModelUnloaded);
    connect(m_worker, &InferenceWorker::sessionStarted,
            this, &DictationController::onSessionStarted);
    connect(m_worker, &InferenceWorker::textFinalized,
            this, &DictationController::onTextFinalized);
    connect(m_worker, &InferenceWorker::sessionFinished,
            this, &DictationController::onSessionFinished);
    connect(m_worker, &InferenceWorker::sessionCancelled,
            this, &DictationController::onSessionCancelled);
}

void DictationController::wireRecorder()
{
    connect(m_recorder, &AudioRecorder::pcmBlock,
            this, &DictationController::onPcmBlock);
    connect(m_recorder, &AudioRecorder::levelChanged,
            this, &DictationController::onLevel);
}

DictationController::DictationController(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_ownRecorder(true)
    , m_ownWorker(true)
{
    initCommon(settings);
    m_recorder = new AudioRecorder(this);
    m_worker = new ParakeetWorker;
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_workerThread.start();
    wireWorker();
    wireRecorder();
    setupTransform(nullptr);
    if (postProcessActive()) {
        ensureTransformModelLoaded();
    }
}

DictationController::DictationController(AppSettings *settings,
                                         InferenceWorker *worker,
                                         AudioRecorder *recorder,
                                         QObject *parent)
    : DictationController(settings, worker, recorder, nullptr, parent)
{
}

DictationController::DictationController(AppSettings *settings,
                                         InferenceWorker *worker,
                                         AudioRecorder *recorder,
                                         TextTransformer *transformer,
                                         QObject *parent)
    : QObject(parent)
    , m_ownRecorder(false)
    , m_ownWorker(false)
{
    initCommon(settings);
    m_recorder = recorder;
    m_worker = worker;
    // Same-thread: no background worker thread. Tests drive processEvents().
    wireWorker();
    wireRecorder();
    setupTransform(transformer);
}

DictationController::~DictationController()
{
    cancel();
    if (m_ownWorker) {
        m_workerThread.quit();
        m_workerThread.wait(3000);
        m_worker = nullptr;
    }
    if (m_ownTransformer) {
        // Unload on the transform thread before quitting it. deleteLater after
        // QThread::finished never runs (no event loop), which caused
        // "pure virtual method called" / SIGABRT on exit.
        if (m_transformWorker && m_transformThread.isRunning()) {
            QMetaObject::invokeMethod(m_transformWorker, "unloadModel",
                                      Qt::BlockingQueuedConnection);
        }
        m_transformThread.quit();
        m_transformThread.wait(5000);
        // Thread is stopped — safe to delete from this thread.
        if (m_transformWorker) {
            m_transformWorker->moveToThread(QThread::currentThread());
            delete m_transformWorker;
            m_transformWorker = nullptr;
        }
        if (m_transformer) {
            m_transformer->moveToThread(QThread::currentThread());
            delete m_transformer;
            m_transformer = nullptr;
        }
    } else if (m_transformWorker) {
        delete m_transformWorker;
        m_transformWorker = nullptr;
    }
    if (m_ownRecorder) {
        m_recorder = nullptr;
    }
}

void DictationController::setInsertionRouter(InsertionRouter *router)
{
    m_inserter = router;
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
    if (m_transformInFlight) {
        qCDebug(keaLog) << "start ignored while polishing";
        return;
    }
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
    m_utteranceBuffer.clear();
    setError(QString());
    setState(State::Starting);
    setStatus(QStringLiteral("Starting…"));
    qCInfo(keaLog) << "beginListening";
    if (postProcessActive()) {
        ensureTransformModelLoaded();
    }
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

bool DictationController::postProcessActive() const
{
    return m_settings && m_settings->postProcessEnabled();
}

void DictationController::ensureTransformModelLoaded()
{
    if (!m_settings || !m_transformWorker || m_transformLoadPending) {
        return;
    }
    // Heal missing / test-polluted paths (e.g. /tmp/kea-fake-llm.gguf) by
    // picking a real GGUF under ~/.local/share/kea/llm-models/.
    const QString path = m_settings->resolveLlmModelPath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        m_transformModelReady = false;
        qCWarning(keaLog) << "LLM model file missing; download LFM under"
                          << AppSettings::defaultLlmModelsDir()
                          << "configured path was" << m_settings->llmModelPath();
        if (postProcessActive()) {
            setStatus(QStringLiteral(
                "LLM model missing — download LFM2.5 in Settings (Transcript mode)"));
        }
        return;
    }
    if (m_transformModelReady) {
        // Already loaded for this session.
        return;
    }
    m_transformLoadPending = true;
    m_transformModelReady = false;
    qCInfo(keaLog) << "loading LLM model" << path;
    setStatus(QStringLiteral("Loading LLM…"));
    QMetaObject::invokeMethod(m_transformWorker, "loadModel", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void DictationController::onTransformModelReady(bool ok, const QString &error)
{
    m_transformLoadPending = false;
    m_transformModelReady = ok;
    if (!ok && postProcessActive()) {
        qCWarning(keaLog) << "LLM model not ready:" << error;
        setStatus(QStringLiteral("LLM load failed: %1").arg(error));
    } else if (ok) {
        qCInfo(keaLog) << "LLM model ready";
        if (!m_transformInFlight && m_state == State::Idle) {
            setStatus(QStringLiteral("Ready — hold hotkey to dictate"));
        }
    }
}

void DictationController::queueTransform(const QString &utterance)
{
    ++m_transformRequestId;
    m_pendingTransformId = m_transformRequestId;
    m_transformInFlight = true;
    setStatus(QStringLiteral("Polishing…"));
    const int style = m_settings ? m_settings->postProcessStyle() : 0;
    // Ensure load is queued *before* transform on the same worker thread so
    // load completes first when both were just requested.
    ensureTransformModelLoaded();
    QMetaObject::invokeMethod(m_transformWorker, "transform", Qt::QueuedConnection,
                              Q_ARG(QString, utterance),
                              Q_ARG(int, style),
                              Q_ARG(quint64, m_pendingTransformId));
}

void DictationController::onTransformFinished(quint64 requestId, const QString &text,
                                              const QString &error)
{
    if (requestId != m_pendingTransformId) {
        qCDebug(keaLog) << "ignoring stale transform result id=" << requestId;
        return;
    }
    m_transformInFlight = false;
    m_pendingTransformId = 0;
    if (!error.isEmpty()) {
        // Fail open: still deliver text (may be raw). Surface the note.
        m_lastError = error;
        Q_EMIT lastErrorChanged();
    } else {
        m_lastError.clear();
        Q_EMIT lastErrorChanged();
    }
    deliverFinalText(text);
}

void DictationController::deliverFinalText(const QString &text)
{
    if (m_inserter) {
        m_inserter->clearPreedit();
    }
    m_utteranceBuffer.clear();

    if (text.isEmpty()) {
        setState(State::Idle);
        setStatus(QStringLiteral("Idle"));
        maybeUnloadAfterDrain();
        return;
    }

    // Idle first so a new hotkey press is allowed during insert.
    setState(State::Idle);
    setStatus(QStringLiteral("Idle"));

    const QString copy = text;
    QMetaObject::invokeMethod(
        this,
        [this, copy]() {
            if (!m_inserter) {
                m_lastError = QStringLiteral("Could not insert text (no inserter)");
                Q_EMIT lastErrorChanged();
                setStatus(QStringLiteral("Insert failed — transcript: %1").arg(copy));
                return;
            }
            const auto r = m_inserter->insertText(copy);
            m_inserter->clearPreedit();
            if (!r.delivered) {
                m_lastError =
                    QStringLiteral("Could not insert text (%1)").arg(r.detail);
                Q_EMIT lastErrorChanged();
                setStatus(QStringLiteral("Insert failed — transcript: %1").arg(copy));
                return;
            }
            if (r.path == InsertionRouter::Path::Clipboard) {
                m_lastError.clear();
                Q_EMIT lastErrorChanged();
                setStatus(r.detail);
            }
        },
        Qt::QueuedConnection);

    maybeUnloadAfterDrain();
}

void DictationController::onTextFinalized(const QString &text, int /*eouMask*/)
{
    if (text.isEmpty() || m_state == State::Idle) {
        return;
    }

    // Gated transform path: show raw ASR as preedit, commit only after stop.
    if (postProcessActive()) {
        if (!m_utteranceBuffer.isEmpty() && !m_utteranceBuffer.endsWith(QLatin1Char(' '))) {
            m_utteranceBuffer += QLatin1Char(' ');
        }
        m_utteranceBuffer += text;
        if (m_inserter) {
            m_inserter->setPreedit(m_utteranceBuffer);
        }
        setStatus(QStringLiteral("Listening (will polish)…"));
        return;
    }

    // Verbatim / passthrough: commit each finalized chunk immediately.
    if (!m_inserter) {
        m_lastError = QStringLiteral("Could not insert text (no inserter)");
        Q_EMIT lastErrorChanged();
        setStatus(m_lastError);
        return;
    }
    const auto r = m_inserter->insertText(text);
    if (!r.delivered) {
        // Do not enter Error state mid-session — terminals often lack an IM context.
        m_lastError = QStringLiteral("Could not insert text (%1)").arg(r.detail);
        Q_EMIT lastErrorChanged();
        setStatus(m_lastError);
        return;
    }
    if (r.path == InsertionRouter::Path::Clipboard) {
        m_lastError.clear();
        Q_EMIT lastErrorChanged();
        setStatus(r.detail);
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

    if (!error.isEmpty() && text.isEmpty() && m_utteranceBuffer.isEmpty()) {
        if (m_inserter) {
            m_inserter->clearPreedit();
        }
        m_utteranceBuffer.clear();
        setError(error);
        maybeUnloadAfterDrain();
        return;
    }

    // Full utterance: streaming buffer + offline/streaming tail from finalize.
    QString full = m_utteranceBuffer;
    if (!text.isEmpty()) {
        if (!full.isEmpty() && !full.endsWith(QLatin1Char(' '))
            && !text.startsWith(QLatin1Char(' '))) {
            full += QLatin1Char(' ');
        }
        full += text;
    }
    full = full.trimmed();
    m_utteranceBuffer.clear();

    if (full.isEmpty()) {
        qCInfo(keaLog) << "transcript empty";
        if (m_inserter) {
            m_inserter->clearPreedit();
        }
        m_lastError.clear();
        Q_EMIT lastErrorChanged();
        setState(State::Idle);
        setStatus(QStringLiteral("Idle"));
        maybeUnloadAfterDrain();
        return;
    }

    qCInfo(keaLog) << "transcript ready, chars=" << full.size()
                    << "postProcess=" << postProcessActive();

    if (postProcessActive()) {
        // Keep Draining-like UX until transform completes; stay non-Idle so
        // start() is ignored while polishing.
        setStatus(QStringLiteral("Polishing…"));
        if (m_inserter) {
            m_inserter->setPreedit(full);
        }
        ensureTransformModelLoaded();
        queueTransform(full);
        return;
    }

    m_lastError.clear();
    Q_EMIT lastErrorChanged();
    deliverFinalText(full);
}

void DictationController::cancel()
{
    if (m_state == State::Idle && !m_transformInFlight) {
        return;
    }
    m_stopWhenStarted = false;
    m_startAfterLoad = false;
    m_utteranceBuffer.clear();
    m_pendingTransformId = 0;
    m_transformInFlight = false;
    stopCaptureOnly();
    if (m_inserter) {
        m_inserter->clearPreedit();
    }
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "cancelSession", Qt::QueuedConnection);
    }
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
