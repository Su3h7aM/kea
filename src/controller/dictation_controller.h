/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * DictationController — the central state machine.
 *
 *   Idle ──start / hotkey-down──▶ Listening ──stop / hotkey-up──▶ Draining ──▶ Idle
 *     ▲                              │ cancel                         │
 *     └──────────────────────────────┴────────────────────────────────┘
 *
 * Owns AudioRecorder (main thread) and a ParakeetWorker on a dedicated QThread.
 * Finalized text is pushed into TextCommitter on the GUI thread.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <memory>

namespace kea {

class AudioRecorder;
class AppSettings;
class GlobalHotkey;
class ParakeetWorker;
class TextCommitter;

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

public:
    enum class State {
        Idle = 0,
        LoadingModel,
        Listening,
        Draining,
        Error,
    };
    Q_ENUM(State)

    explicit DictationController(AppSettings *settings, QObject *parent = nullptr);
    ~DictationController() override;

    void setTextCommitter(TextCommitter *committer);
    void setHotkey(GlobalHotkey *hotkey);

    State state() const { return m_state; }
    int stateInt() const { return static_cast<int>(m_state); }
    QString stateName() const;
    QString statusText() const { return m_statusText; }
    float level() const { return m_level; }
    QString lastError() const { return m_lastError; }
    bool isListening() const { return m_state == State::Listening; }
    bool isModelLoaded() const { return m_modelLoaded; }

public Q_SLOTS:
    /// Ensure the model is loaded (async). Emits modelLoadedChanged when done.
    void loadModel();
    /// Begin a dictation session (push-to-talk down / tray Start).
    void start();
    /// End the session: stop mic, finalize stream, commit tail.
    void stop();
    /// Abort without committing residual preedit.
    void cancel();

Q_SIGNALS:
    void stateChanged();
    void statusTextChanged();
    void levelChanged();
    void lastErrorChanged();
    void modelLoadedChanged();

private Q_SLOTS:
    void onHotkeyActive(bool active);
    void onPcmBlock(const QVector<float> &samples);
    void onLevel(float level);
    void onModelReady(bool ok, const QString &error);
    void onStreamStarted(bool ok, const QString &error);
    void onTextFinalized(const QString &text, int eouMask);
    void onStreamFinished(const QString &tail, const QString &error);
    void onStreamCancelled();

private:
    void setState(State s);
    void setStatus(const QString &text);
    void setError(const QString &err);
    void beginListening();
    void stopCaptureOnly();

    AppSettings *m_settings = nullptr;
    TextCommitter *m_committer = nullptr;
    GlobalHotkey *m_hotkey = nullptr;

    std::unique_ptr<AudioRecorder> m_recorder;
    QThread m_workerThread;
    ParakeetWorker *m_worker = nullptr; // lives on m_workerThread

    State m_state = State::Idle;
    QString m_statusText;
    QString m_lastError;
    float m_level = 0.0f;
    bool m_modelLoaded = false;
    bool m_modelLoadPending = false;
    bool m_startAfterLoad = false;
};

} // namespace kea
