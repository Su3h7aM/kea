/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "text_committer.h"

#include "input_context.h"

namespace kea {

TextCommitter::TextCommitter(QObject *parent)
    : QObject(parent)
{
}

void TextCommitter::setContext(IInputContext *ctx)
{
    const bool was = canCommit();
    m_ctx = ctx;
    m_currentPreedit.clear();
    m_lastError.clear();
    const bool now = canCommit();
    if (was != now) {
        Q_EMIT canCommitChanged(now);
    }
}

bool TextCommitter::canCommit() const
{
    return m_ctx != nullptr && m_ctx->isValid();
}

bool TextCommitter::commitText(const QString &text)
{
    if (!canCommit()) {
        setError(QStringLiteral("no active input context"));
        ++m_skippedCount;
        return false;
    }
    if (text.isEmpty()) {
        return true; // nothing to send, not an error
    }
    m_ctx->commitString(text);
    m_currentPreedit.clear();
    ++m_commitCount;
    m_lastError.clear();
    Q_EMIT committed(text);
    return true;
}

bool TextCommitter::setPreedit(const QString &text)
{
    if (!canCommit()) {
        setError(QStringLiteral("no active input context"));
        ++m_skippedCount;
        return false;
    }
    if (text == m_currentPreedit) {
        return true;
    }
    // Empty fallbackCommit: on unfocus we don't want a partial word stuck.
    m_ctx->setPreedit(text, QString());
    m_currentPreedit = text;
    ++m_preeditCount;
    m_lastError.clear();
    return true;
}

bool TextCommitter::clearPreedit()
{
    if (m_currentPreedit.isEmpty()) {
        return true;
    }
    return setPreedit(QString());
}

void TextCommitter::setError(const QString &msg)
{
    m_lastError = msg;
}

} // namespace kea
