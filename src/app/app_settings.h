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
    Q_PROPERTY(int activationMode READ activationMode WRITE setActivationMode NOTIFY activationModeChanged)
    Q_PROPERTY(bool onboardingDone READ onboardingDone WRITE setOnboardingDone NOTIFY onboardingDoneChanged)
    Q_PROPERTY(bool modelFileExists READ modelFileExists NOTIFY modelPathChanged)

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

    /// True when modelPath points at an existing file.
    bool modelFileExists() const;

    /// Directory that holds downloaded models (~/.local/share/kea/models).
    Q_INVOKABLE QString modelsDir() const;

    void load();
    void save() const;

    /// Default model location under XDG data home.
    static QString defaultModelPath();
    static QString defaultModelsDir();

Q_SIGNALS:
    void modelPathChanged();
    void backendChanged();
    void hotkeyChanged();
    void activationModeChanged();
    void onboardingDoneChanged();

private:
    QString m_modelPath;
    int m_backend = 0;
    QKeySequence m_hotkey;
    int m_activationMode = 0; ///< 0=Push-to-talk, 1=Toggle
    bool m_onboardingDone = false;
};

} // namespace kea
