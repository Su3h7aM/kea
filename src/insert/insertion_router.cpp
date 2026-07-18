/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "insertion_router.h"

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>

#include "text_committer.h"

namespace kea {

InsertionRouter::InsertionRouter(QObject *parent)
    : QObject(parent)
{
}

void InsertionRouter::setTextCommitter(TextCommitter *committer)
{
    if (m_committer == committer) {
        return;
    }
    if (m_committer) {
        disconnect(m_committer, nullptr, this, nullptr);
    }
    m_committer = committer;
    if (m_committer) {
        connect(m_committer, &TextCommitter::canCommitChanged, this,
                &InsertionRouter::canUseInputMethodChanged);
    }
    Q_EMIT canUseInputMethodChanged(canUseInputMethod());
}

void InsertionRouter::setClipboardFallbackEnabled(bool enabled)
{
    if (m_clipboardFallback == enabled) {
        return;
    }
    m_clipboardFallback = enabled;
    Q_EMIT clipboardFallbackEnabledChanged();
}

bool InsertionRouter::canUseInputMethod() const
{
    return m_committer && m_committer->canCommit();
}

InsertionRouter::Result InsertionRouter::insertText(const QString &text)
{
    Result r;
    if (text.isEmpty()) {
        r.delivered = true;
        r.path = Path::None;
        m_lastPath = r.path;
        m_lastDetail.clear();
        return r;
    }

    if (m_committer && m_committer->commitText(text)) {
        r.delivered = true;
        r.path = Path::InputMethod;
        m_lastPath = r.path;
        m_lastDetail.clear();
        qInfo().nospace() << "Kea: insert path=input-method chars=" << text.size();
        Q_EMIT inserted(r.path, text);
        return r;
    }

    const QString imErr = m_committer
        ? (m_committer->lastError().isEmpty()
               ? QStringLiteral("no active text field")
               : m_committer->lastError())
        : QStringLiteral("no text committer");

    if (!m_clipboardFallback) {
        r.delivered = false;
        r.path = Path::None;
        r.detail = imErr;
        m_lastPath = r.path;
        m_lastDetail = r.detail;
        qWarning().nospace() << "Kea: insert failed (IM only): " << imErr
                             << " chars=" << text.size();
        return r;
    }

    QString clipErr;
    if (!copyToClipboard(text, &clipErr)) {
        r.delivered = false;
        r.path = Path::None;
        r.detail = QStringLiteral("%1; clipboard failed: %2").arg(imErr, clipErr);
        m_lastPath = r.path;
        m_lastDetail = r.detail;
        qWarning().nospace() << "Kea: insert failed (IM+clipboard): " << r.detail
                             << " chars=" << text.size();
        return r;
    }

    r.delivered = true;
    r.path = Path::Clipboard;
    r.detail = QStringLiteral(
        "No active text field — transcript copied to clipboard. "
        "Paste with Ctrl+V (or Shift+Insert in many terminals).");
    m_lastPath = r.path;
    m_lastDetail = r.detail;
    qInfo().nospace() << "Kea: insert path=clipboard chars=" << text.size()
                      << " reason=" << imErr;
    Q_EMIT inserted(r.path, text);
    return r;
}

bool InsertionRouter::setPreedit(const QString &text)
{
    return m_committer ? m_committer->setPreedit(text) : false;
}

bool InsertionRouter::clearPreedit()
{
    return m_committer ? m_committer->clearPreedit() : true;
}

bool InsertionRouter::copyToClipboard(const QString &text, QString *errorOut)
{
    QClipboard *clip = QGuiApplication::clipboard();
    if (!clip) {
        if (errorOut) {
            *errorOut = QStringLiteral("no clipboard");
        }
        return false;
    }
    // Prefer Clipboard mode (not Selection) so Ctrl+V works in GTK/Qt/Firefox.
    clip->setText(text, QClipboard::Clipboard);
    // Also seed primary selection for middle-click paste (XWayland / some terminals).
    clip->setText(text, QClipboard::Selection);
    if (clip->text(QClipboard::Clipboard) != text) {
        if (errorOut) {
            *errorOut = QStringLiteral("clipboard write did not stick");
        }
        return false;
    }
    return true;
}

} // namespace kea
