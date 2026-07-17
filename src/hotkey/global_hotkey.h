/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * GlobalHotkey — push-to-talk via KGlobalAccel.
 *
 * Uses KGlobalAccel::globalShortcutActiveChanged so hold-to-talk works: active
 * true on key-down, false on key-up. The action is also triggerable as a
 * one-shot from the tray (toggle start/stop handled by the controller).
 */
#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

class QAction;

namespace kea {

class GlobalHotkey : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QKeySequence sequence READ sequence WRITE setSequence NOTIFY sequenceChanged)
    Q_PROPERTY(QString sequenceDisplay READ sequenceDisplay NOTIFY sequenceChanged)
    Q_PROPERTY(bool registered READ isRegistered NOTIFY registeredChanged)

public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    QKeySequence sequence() const { return m_sequence; }
    QString sequenceDisplay() const { return m_sequence.toString(QKeySequence::NativeText); }
    void setSequence(const QKeySequence &seq);

    bool isRegistered() const { return m_registered; }

    /// Default: Meta+Shift+V (unlikely to collide with system bindings).
    static QKeySequence defaultSequence();

Q_SIGNALS:
    /// true = key held / hotkey pressed; false = released.
    void activeChanged(bool active);
    void sequenceChanged();
    void registeredChanged();
    /// Fired on a discrete activation (e.g. if the shortcut is used as a toggle).
    void triggered();

private:
    void reregister();

    QAction *m_action = nullptr;
    QKeySequence m_sequence;
    bool m_registered = false;
};

} // namespace kea
