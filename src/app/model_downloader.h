/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * ModelDownloader — fetch a GGUF from a URL into the models directory.
 *
 * Progress is exposed for the QML progress bar. On success, emits finished with
 * the local path so AppSettings.modelPath can be updated.
 */
#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace kea {

class ModelDownloader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ModelDownloader(QObject *parent = nullptr);
    ~ModelDownloader() override;

    bool isBusy() const { return m_busy; }
    qreal progress() const { return m_progress; }
    QString statusText() const { return m_statusText; }
    QString lastError() const { return m_lastError; }

public Q_SLOTS:
    /// Download `url` into `destPath` (absolute). Overwrites if present.
    void download(const QString &url, const QString &destPath);
    void cancel();

Q_SIGNALS:
    void busyChanged();
    void progressChanged();
    void statusTextChanged();
    void lastErrorChanged();
    void finished(const QString &localPath);
    void failed(const QString &error);

private:
    void setBusy(bool busy);
    void setProgress(qreal p);
    void setStatus(const QString &s);
    void setError(const QString &e);
    void cleanupReply();

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    bool m_busy = false;
    qreal m_progress = 0.0;
    QString m_statusText;
    QString m_lastError;
    QString m_destPath;
};

} // namespace kea
