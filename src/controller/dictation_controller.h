/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * DictationController — the central state machine.
 *
 *   Idle ──start / hotkey-down──▶ Starting ──▶ Listening ──stop / hotkey-up──▶ Draining ──▶ Idle
 *
 * Critical: `start()` must enter Starting *synchronously* so overlapping hotkey
 * events cannot open multiple mic/parakeet sessions (that path SIGSEGV'd).
 *
 * Push-to-talk uses only GlobalHotkey::activeChanged (press/release). The
 * discrete `triggered` signal is ignored for PTT to avoid double start/stop.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QThread>

namespace kea {

class AudioRecorder;
class AppSettings;
class GlobalHotkey;
class InferenceWorker;
class InsertionRouter;
class TextTransformer;
class TransformWorker;

class DictationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(float level READ level NOTIFY levelChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool listening READ isListening NOTIFY stateChanged)
    Q_PROPERTY(bool modelLoaded READ isModelLoaded NOTIFY modelLoadedChanged)
    Q_PROPERTY(bool canConfigure READ canConfigure NOTIFY modelLoadedChanged)
    Q_PROPERTY(int activationMode READ activationModeInt NOTIFY activationModeChanged)

public:
    enum class State {
        Idle = 0,
        LoadingModel,
        Starting,
        Listening,
        Draining,
        Error,
    };
    Q_ENUM(State)

    /// Production: owns a ParakeetWorker on a dedicated thread + AudioRecorder
    /// + TransformWorker (LLM post-process on its own thread).
    explicit DictationController(AppSettings *settings, QObject *parent = nullptr);

    /// Test seam: external ASR worker + recorder (not owned; same-thread).
    DictationController(AppSettings *settings,
                        InferenceWorker *worker,
                        AudioRecorder *recorder,
                        QObject *parent = nullptr);

    /// Test seam: also inject a TextTransformer (runs same-thread via TransformWorker).
    DictationController(AppSettings *settings,
                        InferenceWorker *worker,
                        AudioRecorder *recorder,
                        TextTransformer *transformer,
                        QObject *parent = nullptr);

    ~DictationController() override;

    void setInsertionRouter(InsertionRouter *router);
    void setHotkey(GlobalHotkey *hotkey);

    State state() const { return m_state; }
    int stateInt() const { return static_cast<int>(m_state); }
    QString stateName() const;
    QString statusText() const { return m_statusText; }
    float level() const { return m_level; }
    QString lastError() const { return m_lastError; }
    bool isListening() const
    {
        return m_state == State::Listening || m_state == State::Starting;
    }
    bool isModelLoaded() const { return m_modelLoaded; }
    /// True when the model is NOT loaded — settings (model path, backend) are
    /// editable. False once Start loads the model; true again after Stop.
    bool canConfigure() const { return !m_modelLoaded && !m_modelLoadPending; }

    /// 0 = Push-to-talk (hold), 1 = Toggle (press to start/stop)
    int activationModeInt() const { return static_cast<int>(m_activationMode); }
    enum class ActivationMode { PushToTalk = 0, Toggle = 1 };

public Q_SLOTS:
    void loadModel();
    void unloadModel();
    void start();
    void stop();
    void cancel();
    void setActivationMode(int mode);

Q_SIGNALS:
    void stateChanged();
    void statusTextChanged();
    void levelChanged();
    void lastErrorChanged();
    void modelLoadedChanged();
    void activationModeChanged();

private Q_SLOTS:
    void onHotkeyActive(bool active);
    void onHotkeyTriggered();
    void onPcmBlock(const QList<float> &samples);
    void onLevel(float level);
    void onModelReady(bool ok, const QString &error);
    void onModelUnloaded();
    void onSessionStarted(bool ok, const QString &error, bool offlineMode);
    void onTextFinalized(const QString &text, int eouMask);
    void onSessionFinished(const QString &text, const QString &error);
    void onSessionCancelled();
    void onTransformFinished(quint64 requestId, const QString &text, const QString &error);
    void onTransformModelReady(bool ok, const QString &error);

private:
    void initCommon(AppSettings *settings);
    void wireWorker();
    void wireRecorder();
    void setupTransform(TextTransformer *externalTransformer);
    void setState(State s);
    void setStatus(const QString &text);
    void setError(const QString &err);
    void beginListening();
    void stopCaptureOnly();
    /// Tear down the worker backend without changing controller state or status.
    void teardownBackend();
    bool isBusy() const;
    /// If unloadModel() was deferred because a Draining session was still in
    /// flight, actually unload now that the session has resolved.
    void maybeUnloadAfterDrain();
    bool postProcessActive() const;
    void ensureTransformModelLoaded();
    void deliverFinalText(const QString &text);
    void queueTransform(const QString &utterance);

    AppSettings *m_settings = nullptr;
    InsertionRouter *m_inserter = nullptr;
    GlobalHotkey *m_hotkey = nullptr;

    AudioRecorder *m_recorder = nullptr;
    bool m_ownRecorder = true;

    QThread m_workerThread;
    InferenceWorker *m_worker = nullptr;
    bool m_ownWorker = true;

    QThread m_transformThread;
    TransformWorker *m_transformWorker = nullptr;
    TextTransformer *m_transformer = nullptr;
    bool m_ownTransformer = true;
    bool m_transformModelReady = false;
    bool m_transformLoadPending = false;

    State m_state = State::Idle;
    QString m_statusText;
    QString m_lastError;
    float m_level = 0.0f;
    bool m_modelLoaded = false;
    bool m_modelLoadPending = false;
    bool m_startAfterLoad = false;
    bool m_stopWhenStarted = false; ///< release arrived while still Starting
    bool m_offlineMode = false;
    /// unloadModel() was called while Draining (a transcription is still
    /// running on the worker thread); actually unload once it resolves.
    bool m_unloadAfterDrain = false;
    ActivationMode m_activationMode = ActivationMode::PushToTalk;

    /// Buffered ASR text while post-process is on (shown via preedit).
    QString m_utteranceBuffer;
    quint64 m_transformRequestId = 0;
    quint64 m_pendingTransformId = 0;
    bool m_transformInFlight = false;
};

} // namespace kea
