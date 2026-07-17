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
    m_item->setIconByName(QStringLiteral("preferences-desktop-locale"));
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setToolTip(QStringLiteral("preferences-desktop-locale"),
                       QStringLiteral("Kea"),
                       QStringLiteral("Voice dictation"));
    m_item->setStandardActionsEnabled(false);

    // Context menu (KF6 builds the tray menu via setContextMenu).
    m_menu = new QMenu;

    auto *settingsAction = m_menu->addAction(i18nc("@action:inmenu", "Settings"));
    connect(settingsAction, &QAction::triggered, this, &TrayController::showWindowRequested);

    m_menu->addSeparator();

    auto *quitAction = m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                         i18nc("@action:inmenu", "Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_item->setContextMenu(m_menu);

    setStatusText(QStringLiteral("Idle"));
}

TrayController::~TrayController()
{
    delete m_menu;
}

void TrayController::show()
{
    // KStatusNotifierItem registers itself on construction; nothing extra to do
    // for Phase 0. The dictation state machine will drive statusText later.
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
