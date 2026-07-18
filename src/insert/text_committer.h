/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * TextCommitter — serial-safe orchestration of commit_string / preedit.
 *
 * Invariant (RFC risk R2): never send a commit/preedit without a valid
 * IInputContext. Long strings are split into UTF-8-safe chunks (fcitx5-style
 * 4k limit) so a single offline transcript cannot blow the Wayland message
 * size. All IM insertion traffic goes through this class.
 */
#pragma once

#include <QObject>
#include <QString>

namespace kea {

class IInputContext;

class TextCommitter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canCommit READ canCommit NOTIFY canCommitChanged)

public:
    explicit TextCommitter(QObject *parent = nullptr);

    /// Attach / replace the active input context. Pass nullptr on deactivate.
    void setContext(IInputContext *ctx);

    IInputContext *context() const { return m_ctx; }

    /// True when a live context is attached and ready for commits.
    bool canCommit() const;

    /// Commit finalized text into the focused field. No-op if !canCommit() or
    /// text is empty. Returns true if a commit was sent.
    bool commitText(const QString &text);

    /// Update the live preedit (not-yet-finalized ASR tail). Empty string clears.
    bool setPreedit(const QString &text);

    /// Clear any visible preedit without committing.
    bool clearPreedit();

    /// Last error message (empty when last op succeeded).
    QString lastError() const { return m_lastError; }

    /// Counters for unit tests / diagnostics.
    int commitCount() const { return m_commitCount; }
    int preeditCount() const { return m_preeditCount; }
    int skippedCount() const { return m_skippedCount; }

Q_SIGNALS:
    void canCommitChanged(bool can);
    void committed(const QString &text);

private:
    void setError(const QString &msg);

    IInputContext *m_ctx = nullptr;
    QString m_lastError;
    QString m_currentPreedit;
    int m_commitCount = 0;
    int m_preeditCount = 0;
    int m_skippedCount = 0;
};

} // namespace kea
