/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "global_hotkey.h"

#include <QAction>

#include <KGlobalAccel>
#include <KLocalizedString>

namespace kea {

QKeySequence GlobalHotkey::defaultSequence()
{
    // Meta+Ctrl+X — two adjacent modifiers (palm on bottom-left) + X (no need
    // to reach far). Mirrors the ergonomic pattern of Wispr Flow's Windows
    // default (Ctrl+Win), adapted for Plasma's Meta convention.
    return QKeySequence(Qt::META | Qt::CTRL | Qt::Key_X);
}

GlobalHotkey::GlobalHotkey(QObject *parent)
    : QObject(parent)
    , m_action(new QAction(this))
    , m_sequence(defaultSequence())
{
    m_action->setObjectName(QStringLiteral("pushToTalk"));
    m_action->setText(i18nc("@action", "Push to talk"));
    // Component name used by System Settings → Shortcuts.
    m_action->setProperty("componentName", QStringLiteral("kea"));
    m_action->setProperty("componentDisplayName", QStringLiteral("Kea"));

    connect(m_action, &QAction::triggered, this, &GlobalHotkey::triggered);

    // Hold-to-talk: active=true on press, false on release.
    connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutActiveChanged,
            this, [this](QAction *action, bool active) {
                if (action == m_action) {
                    Q_EMIT activeChanged(active);
                }
            });

    reregister();
}

GlobalHotkey::~GlobalHotkey()
{
    if (m_action) {
        KGlobalAccel::self()->removeAllShortcuts(m_action);
    }
}

void GlobalHotkey::setSequence(const QKeySequence &seq)
{
    if (seq == m_sequence) {
        return;
    }
    m_sequence = seq.isEmpty() ? defaultSequence() : seq;
    reregister();
    Q_EMIT sequenceChanged();
}

void GlobalHotkey::reregister()
{
    const QList<QKeySequence> keys{m_sequence};
    // setDefaultShortcut seeds the system-settings default; setShortcut registers.
    KGlobalAccel::self()->setDefaultShortcut(m_action, keys);
    const bool ok = KGlobalAccel::self()->setShortcut(m_action, keys, KGlobalAccel::NoAutoloading);
    // Also try with Autoloading so a user-changed binding from System Settings
    // is preferred on subsequent launches when we switch load flag later.
    if (!ok) {
        KGlobalAccel::self()->setShortcut(m_action, keys);
    }
    const bool now = !KGlobalAccel::self()->shortcut(m_action).isEmpty();
    if (now != m_registered) {
        m_registered = now;
        Q_EMIT registeredChanged();
    }
}

} // namespace kea
