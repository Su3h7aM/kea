/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Lightweight settings persisted via QSettings (org.kde.kea).
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

    void load();
    void save() const;

    /// Default model location under XDG data home.
    static QString defaultModelPath();

Q_SIGNALS:
    void modelPathChanged();
    void backendChanged();
    void hotkeyChanged();

private:
    QString m_modelPath;
    int m_backend = 0;
    QKeySequence m_hotkey;
};

} // namespace kea
