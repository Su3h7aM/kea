/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "model_downloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "logging.h"

namespace kea {

ModelDownloader::ModelDownloader(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_statusText = QStringLiteral("Idle");
}

ModelDownloader::~ModelDownloader()
{
    cancel();
}

void ModelDownloader::download(const QString &url, const QString &destPath)
{
    if (m_busy) {
        setError(QStringLiteral("download already in progress"));
        return;
    }
    if (url.isEmpty() || destPath.isEmpty()) {
        setError(QStringLiteral("url and destination path are required"));
        Q_EMIT failed(m_lastError);
        return;
    }

    const QFileInfo fi(destPath);
    QDir().mkpath(fi.absolutePath());

    // Write to a temp file, rename on success.
    const QString tmpPath = destPath + QStringLiteral(".partial");
    delete m_file;
    m_file = new QFile(tmpPath, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(QStringLiteral("cannot write %1").arg(tmpPath));
        Q_EMIT failed(m_lastError);
        delete m_file;
        m_file = nullptr;
        return;
    }

    m_destPath = destPath;
    setBusy(true);
    setProgress(0.0);
    setStatus(QStringLiteral("Downloading…"));
    setError(QString());

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    // HuggingFace large files need a reasonable UA.
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kea/0.1"));

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 rec, qint64 total) {
        if (total > 0) {
            setProgress(static_cast<qreal>(rec) / static_cast<qreal>(total));
            setStatus(QStringLiteral("Downloading… %1%").arg(int(m_progress * 100)));
        } else {
            setStatus(QStringLiteral("Downloading… %1 MB").arg(rec / (1024 * 1024)));
        }
    });
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file && m_reply) {
            m_file->write(m_reply->readAll());
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) {
            return;
        }
        const auto err = m_reply->error();
        if (err != QNetworkReply::NoError) {
            const QString msg = m_reply->errorString();
            // QNetworkReply::abort() emits finished() synchronously on some
            // backends, so a user-requested cancel() lands here as an
            // OperationCanceledError, not a real failure. Report it as
            // "Cancelled", not "Failed", and skip failed()/lastError.
            const bool wasCancelling = m_cancelling;
            cleanupReply();
            if (m_file) {
                m_file->close();
                m_file->remove();
                delete m_file;
                m_file = nullptr;
            }
            setBusy(false);
            if (wasCancelling) {
                setStatus(QStringLiteral("Cancelled"));
            } else {
                setStatus(QStringLiteral("Failed"));
                setError(msg);
                Q_EMIT failed(msg);
            }
            return;
        }
        if (m_file) {
            m_file->write(m_reply->readAll());
            m_file->close();
            // Atomic-ish replace.
            QFile::remove(m_destPath);
            if (!m_file->rename(m_destPath)) {
                // rename across filesystems can fail; fall back to copy.
                QFile::copy(m_file->fileName(), m_destPath);
                m_file->remove();
            }
            delete m_file;
            m_file = nullptr;
        }
        cleanupReply();
        setBusy(false);
        setProgress(1.0);
        setStatus(QStringLiteral("Downloaded"));
        qCInfo(keaLog) << "model downloaded to" << m_destPath;
        Q_EMIT finished(m_destPath);
    });

    qCInfo(keaLog) << "starting model download" << url << "->" << destPath;
}

void ModelDownloader::cancel()
{
    if (m_reply) {
        // Set before abort(): some backends emit finished() synchronously from
        // inside abort(), and the finished-lambda above needs to see this.
        m_cancelling = true;
        m_reply->abort();
    }
    cleanupReply();
    if (m_file) {
        m_file->close();
        m_file->remove();
        delete m_file;
        m_file = nullptr;
    }
    if (m_busy) {
        setBusy(false);
        setStatus(QStringLiteral("Cancelled"));
    }
    m_cancelling = false;
}

void ModelDownloader::cleanupReply()
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void ModelDownloader::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

void ModelDownloader::setProgress(qreal p)
{
    if (qFuzzyCompare(m_progress, p)) {
        return;
    }
    m_progress = p;
    Q_EMIT progressChanged();
}

void ModelDownloader::setStatus(const QString &s)
{
    if (m_statusText == s) {
        return;
    }
    m_statusText = s;
    Q_EMIT statusTextChanged();
}

void ModelDownloader::setError(const QString &e)
{
    m_lastError = e;
    Q_EMIT lastErrorChanged();
}

} // namespace kea
