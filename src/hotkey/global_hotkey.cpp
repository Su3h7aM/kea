/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "global_hotkey.h"

#include <QAction>

#include <KGlobalAccel>
#include <KLocalizedString>

#include "logging.h"

namespace kea {

QKeySequence GlobalHotkey::defaultSequence()
{
    // Ctrl+Shift+D (D for Dictation). Two left-side modifiers + D under the
    // middle finger. Avoids Meta key (some Plasma setups intercept Meta+key
    // at the compositor level, causing KGlobalAccel to miss the event).
    return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D);
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
    // Standard KDE pattern: setDefaultShortcut seeds the System Settings
    // default; setShortcut (without NoAutoloading) respects any user override
    // from System Settings while registering our key on first run.
    KGlobalAccel::self()->setDefaultShortcut(m_action, keys, KGlobalAccel::NoAutoloading);
    KGlobalAccel::self()->setShortcut(m_action, keys, KGlobalAccel::NoAutoloading);

    const bool now = !KGlobalAccel::self()->shortcut(m_action).isEmpty();
    qCInfo(keaLog) << "hotkey registered:" << m_sequence.toString()
                    << "active=" << now;
    if (now != m_registered) {
        m_registered = now;
        Q_EMIT registeredChanged();
    }
}

} // namespace kea
