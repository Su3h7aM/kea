/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Lightweight settings persisted via QSettings (kea/kea, see load()/save()).
 */
#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

namespace kea {

class AppSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)
    Q_PROPERTY(int backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString hotkey READ hotkeyString WRITE setHotkeyString NOTIFY hotkeyChanged)
    /// Same shortcut as `hotkey`, for QML controls that speak `QKeySequence`
    /// (e.g. KeySequenceItem) without string round-trips.
    Q_PROPERTY(QKeySequence hotkeySequence READ hotkey WRITE setHotkey NOTIFY hotkeyChanged)
    Q_PROPERTY(int activationMode READ activationMode WRITE setActivationMode NOTIFY activationModeChanged)
    Q_PROPERTY(bool onboardingDone READ onboardingDone WRITE setOnboardingDone NOTIFY onboardingDoneChanged)
    Q_PROPERTY(bool modelFileExists READ modelFileExists NOTIFY modelPathChanged)
    /// When true, ASR text is rewritten by the LLM before commit (issue #6).
    Q_PROPERTY(bool postProcessEnabled READ postProcessEnabled WRITE setPostProcessEnabled
                   NOTIFY postProcessEnabledChanged)
    /// TransformStyle: 0=Correct (default), 1=Enhance, 2=Professional, 3=Casual.
    Q_PROPERTY(int postProcessStyle READ postProcessStyle WRITE setPostProcessStyle
                   NOTIFY postProcessStyleChanged)
    Q_PROPERTY(QString llmModelPath READ llmModelPath WRITE setLlmModelPath NOTIFY llmModelPathChanged)
    Q_PROPERTY(bool llmModelFileExists READ llmModelFileExists NOTIFY llmModelPathChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString modelPath() const { return m_modelPath; }
    void setModelPath(const QString &path);

    /// 0 = CPU, 1 = Vulkan
    int backend() const { return m_backend; }
    void setBackend(int backend);

    QKeySequence hotkey() const { return m_hotkey; }
    void setHotkey(const QKeySequence &seq);

    QString hotkeyString() const { return m_hotkey.toString(QKeySequence::NativeText); }
    void setHotkeyString(const QString &s);

    /// 0 = Push-to-talk (hold), 1 = Toggle (press to start/stop)
    int activationMode() const { return m_activationMode; }
    void setActivationMode(int mode);

    bool onboardingDone() const { return m_onboardingDone; }
    void setOnboardingDone(bool done);

    bool postProcessEnabled() const { return m_postProcessEnabled; }
    void setPostProcessEnabled(bool enabled);

    int postProcessStyle() const { return m_postProcessStyle; }
    void setPostProcessStyle(int style);

    QString llmModelPath() const { return m_llmModelPath; }
    void setLlmModelPath(const QString &path);
    bool llmModelFileExists() const;

    /// If the configured path is missing, pick an existing GGUF under the LLM
    /// models dir (or the default path). Returns the path to use for loading;
    /// may update llmModelPath when a better file is found.
    Q_INVOKABLE QString resolveLlmModelPath();

    /// True when modelPath points at an existing file.
    bool modelFileExists() const;

    /// Directory that holds downloaded ASR models (~/.local/share/kea/models).
    Q_INVOKABLE QString modelsDir() const;
    /// Directory for LLM GGUFs (~/.local/share/kea/llm-models).
    Q_INVOKABLE QString llmModelsDir() const;

    void load();
    void save() const;

    /// Default model location under XDG data home.
    static QString defaultModelPath();
    static QString defaultModelsDir();
    static QString defaultLlmModelPath();
    static QString defaultLlmModelsDir();

Q_SIGNALS:
    void modelPathChanged();
    void backendChanged();
    void hotkeyChanged();
    void activationModeChanged();
    void onboardingDoneChanged();
    void postProcessEnabledChanged();
    void postProcessStyleChanged();
    void llmModelPathChanged();

private:
    QString m_modelPath;
    int m_backend = 0;
    QKeySequence m_hotkey;
    int m_activationMode = 0; ///< 0=Push-to-talk, 1=Toggle
    bool m_onboardingDone = false;
    bool m_postProcessEnabled = false;
    int m_postProcessStyle = 0; ///< Correct
    QString m_llmModelPath;
};

} // namespace kea
