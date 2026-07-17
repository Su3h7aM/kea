/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * TrayController — system-tray presence for Kea.
 *
 * Phase 0: a minimal KStatusNotifierItem that reflects app state (idle) and
 * exposes show/quit actions. It will grow to reflect the dictation state
 * machine (idle / listening / error) in later phases.
 */
#pragma once

#include <QObject>
#include <QString>

class KStatusNotifierItem;
class QMenu;

class TrayController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit TrayController(QObject *parent = nullptr);
    ~TrayController() override;

    void show();

    QString statusText() const { return m_statusText; }
    void setStatusText(const QString &text);

Q_SIGNALS:
    void statusTextChanged();
    void showWindowRequested();

private:
    KStatusNotifierItem *m_item = nullptr;
    QMenu *m_menu = nullptr;
    QString m_statusText;
};
