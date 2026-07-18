/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "insertion_router.h"

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>

#include "fake_input.h"
#include "text_committer.h"

namespace kea {

InsertionRouter::InsertionRouter(QObject *parent)
    : QObject(parent)
    , m_fakeInput(std::make_unique<FakeInputClient>(this))
{
}

InsertionRouter::~InsertionRouter() = default;

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
        qWarning() << "Kea: insert IM skipped — TextCommitter not attached";
        return false;
    }
    if (!m_committer->canCommit()) {
        if (errorOut) {
            *errorOut = QStringLiteral(
                "no text-input context (focused app did not enable text-input / IM)");
        }
        qWarning().nospace()
            << "Kea: insert IM unavailable — no text-input context "
            << "(focused client did not enable text-input; "
            << "KWin never activated input-method-v1). chars=" << text.size();
        return false;
    }
    if (m_committer->commitText(text)) {
        return true;
    }
    const QString err = m_committer->lastError().isEmpty()
        ? QStringLiteral("commit_string failed with live context")
        : m_committer->lastError();
    if (errorOut) {
        *errorOut = err;
    }
    qWarning().nospace() << "Kea: insert IM failed with live context: " << err
                         << " chars=" << text.size();
    return false;
}

bool InsertionRouter::tryFakeInput(const QString &text, QString *errorOut)
{
    if (!m_fakeInput) {
        if (errorOut) {
            *errorOut = QStringLiteral("fake_input not constructed");
        }
        return false;
    }
    if (!m_fakeInput->protocolAvailable()) {
        if (errorOut) {
            *errorOut = QStringLiteral(
                "fake_input not bound (need X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input "
                "on installed .desktop; KWin blacklists it otherwise)");
        }
        qWarning()
            << "Kea: insert fake_input skipped — protocol not bound."
            << "KWin hides org_kde_kwin_fake_input unless the app desktop file"
            << "declares X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input.";
        return false;
    }
    if (!m_fakeInput->typeText(text)) {
        if (errorOut) {
            *errorOut = QStringLiteral("fake_input could not type text (no keysym or denied)");
        }
        return false;
    }
    return true;
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

    QString err1;
    if (tryInputMethod(text, &err1)) {
        r.delivered = true;
        r.path = Path::InputMethod;
        m_lastPath = r.path;
        m_lastDetail.clear();
        qInfo().nospace() << "Kea: insert path=input-method chars=" << text.size();
        Q_EMIT inserted(r.path, text);
        return r;
    }
    qInfo().nospace() << "Kea: insert chain step1 IM failed — \"" << err1
                      << "\" chars=" << text.size() << " → trying fake_input";

    QString err2;
    if (tryFakeInput(text, &err2)) {
        r.delivered = true;
        r.path = Path::FakeInput;
        m_lastPath = r.path;
        m_lastDetail.clear();
        qInfo().nospace() << "Kea: insert path=fake_input chars=" << text.size();
        Q_EMIT inserted(r.path, text);
        return r;
    }
    qInfo().nospace() << "Kea: insert chain step2 fake_input failed — \"" << err2
                      << "\" chars=" << text.size() << " → trying clipboard";

    QString err3;
    if (tryClipboardLastResort(text, &err3)) {
        r.delivered = true;
        r.path = Path::Clipboard;
        r.detail = QStringLiteral(
            "Could not insert into the focused app — transcript is on the clipboard. "
            "Paste with Ctrl+V (or Shift+Insert in many terminals).");
        m_lastPath = r.path;
        m_lastDetail = r.detail;
        qInfo().nospace()
            << "Kea: insert path=clipboard (last resort) chars=" << text.size()
            << " im=\"" << err1 << "\" fake_input=\"" << err2 << "\"";
        Q_EMIT inserted(r.path, text);
        return r;
    }

    r.delivered = false;
    r.path = Path::None;
    r.detail = QStringLiteral("IM: %1; fake_input: %2; clipboard: %3")
                   .arg(err1, err2, err3);
    m_lastPath = r.path;
    m_lastDetail = r.detail;
    qWarning().nospace() << "Kea: insert failed all paths — " << r.detail
                         << " chars=" << text.size();
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
