/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "tray_controller.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>

#include <KLocalizedString>
#include <KStatusNotifierItem>

TrayController::TrayController(QObject *parent)
    : QObject(parent)
    , m_item(new KStatusNotifierItem(this))
{
    m_item->setTitle(QStringLiteral("Kea"));
    m_item->setIconByName(QStringLiteral("audio-input-microphone"));
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setToolTip(QStringLiteral("audio-input-microphone"),
                       QStringLiteral("Kea"),
                       QStringLiteral("Voice dictation"));
    m_item->setStandardActionsEnabled(false);

    m_menu = new QMenu;

    m_startStopAction = m_menu->addAction(i18nc("@action:inmenu", "Start dictation"));
    connect(m_startStopAction, &QAction::triggered, this, [this]() {
        if (m_listening) {
            Q_EMIT stopRequested();
        } else {
            Q_EMIT startRequested();
        }
    });

    auto *cancelAction = m_menu->addAction(i18nc("@action:inmenu", "Cancel"));
    connect(cancelAction, &QAction::triggered, this, &TrayController::cancelRequested);

    m_menu->addSeparator();

    auto *settingsAction = m_menu->addAction(i18nc("@action:inmenu", "Settings"));
    connect(settingsAction, &QAction::triggered, this, &TrayController::showWindowRequested);

    m_menu->addSeparator();

    auto *quitAction = m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                         i18nc("@action:inmenu", "Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_item->setContextMenu(m_menu);

    // Clicking the tray icon toggles start/stop as a hotkey fallback.
    connect(m_item, &KStatusNotifierItem::activateRequested, this, [this](bool /*active*/, const QPoint &) {
        if (m_listening) {
            Q_EMIT stopRequested();
        } else {
            Q_EMIT startRequested();
        }
    });

    setStatusText(QStringLiteral("Idle"));
}

TrayController::~TrayController()
{
    delete m_menu;
}

void TrayController::show()
{
}

void TrayController::setStatusText(const QString &text)
{
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    m_item->setToolTipSubTitle(text);
    Q_EMIT statusTextChanged();
}

void TrayController::setListening(bool listening)
{
    if (m_listening == listening) {
        return;
    }
    m_listening = listening;
    // Keep a stable icon — swapping icons during capture has been flaky with
    // some status-notifier hosts. Reflect state in the tooltip instead.
    m_item->setToolTipSubTitle(listening ? QStringLiteral("Listening…") : m_statusText);
    updateMenuLabels();
    Q_EMIT listeningChanged();
}

void TrayController::updateMenuLabels()
{
    if (!m_startStopAction) {
        return;
    }
    m_startStopAction->setText(m_listening
                                   ? i18nc("@action:inmenu", "Stop dictation")
                                   : i18nc("@action:inmenu", "Start dictation"));
}
