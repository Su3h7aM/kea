/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * TrayController — system-tray presence for Kea.
 */
#pragma once

#include <QObject>
#include <QString>

class KStatusNotifierItem;
class QMenu;
class QAction;

class TrayController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool listening READ isListening WRITE setListening NOTIFY listeningChanged)

public:
    explicit TrayController(QObject *parent = nullptr);
    ~TrayController() override;

    void show();

    QString statusText() const { return m_statusText; }
    void setStatusText(const QString &text);

    bool isListening() const { return m_listening; }
    void setListening(bool listening);

Q_SIGNALS:
    void statusTextChanged();
    void listeningChanged();
    void showWindowRequested();
    void startRequested();
    void stopRequested();
    void cancelRequested();

private:
    void updateMenuLabels();

    KStatusNotifierItem *m_item = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_startStopAction = nullptr;
    QString m_statusText;
    bool m_listening = false;
};
