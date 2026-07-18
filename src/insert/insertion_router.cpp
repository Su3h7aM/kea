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

bool InsertionRouter::tryInputMethod(const QString &text, QString *errorOut)
{
    if (!m_committer) {
        if (errorOut) {
            *errorOut = QStringLiteral("no text committer");
        }
        return false;
    }
    if (m_committer->commitText(text)) {
        return true;
    }
    if (errorOut) {
        *errorOut = m_committer->lastError().isEmpty()
            ? QStringLiteral("no active text field")
            : m_committer->lastError();
    }
    return false;
}

bool InsertionRouter::tryClipboardLastResort(const QString &text, QString *errorOut)
{
    QClipboard *clip = QGuiApplication::clipboard();
    if (!clip) {
        if (errorOut) {
            *errorOut = QStringLiteral("no clipboard");
        }
        return false;
    }
    // Clipboard mode for Ctrl+V; primary selection for middle-click paste.
    clip->setText(text, QClipboard::Clipboard);
    clip->setText(text, QClipboard::Selection);
    if (clip->text(QClipboard::Clipboard) != text) {
        if (errorOut) {
            *errorOut = QStringLiteral("clipboard write did not stick");
        }
        return false;
    }
    return true;
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

    // --- Step 1: input method (preferred) ---------------------------------
    QString stepErr;
    if (tryInputMethod(text, &stepErr)) {
        r.delivered = true;
        r.path = Path::InputMethod;
        m_lastPath = r.path;
        m_lastDetail.clear();
        qInfo().nospace() << "Kea: insert path=input-method chars=" << text.size();
        Q_EMIT inserted(r.path, text);
        return r;
    }
    const QString priorFailure = stepErr;

    // --- Step 2: future inject backends (key inject / portal / helper) ----
    // Intentionally empty. When added, try them here and return Path::Inject
    // on success. Do not put clipboard above this step.

    // --- Step 3: clipboard (last resort only) -----------------------------
    if (!m_clipboardFallback) {
        r.delivered = false;
        r.path = Path::None;
        r.detail = priorFailure;
        m_lastPath = r.path;
        m_lastDetail = r.detail;
        qWarning().nospace() << "Kea: insert failed (no last-resort clipboard): "
                             << priorFailure << " chars=" << text.size();
        return r;
    }

    QString clipErr;
    if (!tryClipboardLastResort(text, &clipErr)) {
        r.delivered = false;
        r.path = Path::None;
        r.detail = QStringLiteral("%1; clipboard failed: %2").arg(priorFailure, clipErr);
        m_lastPath = r.path;
        m_lastDetail = r.detail;
        qWarning().nospace() << "Kea: insert failed (IM + last-resort clipboard): "
                             << r.detail << " chars=" << text.size();
        return r;
    }

    r.delivered = true;
    r.path = Path::Clipboard;
    r.detail = QStringLiteral(
        "Could not insert into the focused app — transcript is on the clipboard "
        "(last-resort fallback). Paste with Ctrl+V (or Shift+Insert in many terminals).");
    m_lastPath = r.path;
    m_lastDetail = r.detail;
    qInfo().nospace() << "Kea: insert path=clipboard (last resort) chars=" << text.size()
                      << " reason=" << priorFailure;
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

} // namespace kea
