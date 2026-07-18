/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-committer-test — headless unit tests for TextCommitter.
 *
 * Uses a mock IInputContext so the serial/skip invariants are checked without
 * a Wayland compositor. Returns 0 on success, 1 on any failure.
 */
#include <cstdio>
#include <vector>

#include <QCoreApplication>
#include <QString>

#include "insert/commit_chunks.h"
#include "insert/input_context.h"
#include "insert/text_committer.h"

using namespace kea;

static int failures = 0;

static void check(bool cond, const char *msg)
{
    if (cond) {
        std::printf("  ok   %s\n", msg);
    } else {
        std::printf("  FAIL %s\n", msg);
        ++failures;
    }
}

/// Records every commit/preedit so the test can assert on them.
class MockContext : public IInputContext
{
public:
    bool valid = true;
    uint32_t ser = 0;
    std::vector<QString> commits;
    std::vector<QString> preedits;

    bool isValid() const override { return valid; }
    uint32_t serial() const override { return ser; }

    void commitString(const QString &text) override
    {
        commits.push_back(text);
    }

    void setPreedit(const QString &text, const QString & /*fallback*/) override
    {
        preedits.push_back(text);
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::printf("[committer] skip when no context\n");
    {
        TextCommitter c;
        check(!c.canCommit(), "canCommit false with no context");
        check(!c.commitText(QStringLiteral("hello")), "commitText skipped");
        check(c.skippedCount() == 1, "skippedCount == 1");
        check(c.commitCount() == 0, "commitCount still 0");
        check(!c.lastError().isEmpty(), "lastError set");
    }

    std::printf("[committer] commit and preedit with live mock\n");
    {
        MockContext mock;
        mock.ser = 7;
        TextCommitter c;
        c.setContext(&mock);
        check(c.canCommit(), "canCommit true");
        check(c.commitText(QStringLiteral("hello")), "commitText ok");
        check(c.commitCount() == 1, "commitCount == 1");
        check(mock.commits.size() == 1 && mock.commits[0] == QStringLiteral("hello"),
              "mock received 'hello'");
        check(c.commitText(QString()), "empty commit is a no-op success");
        check(mock.commits.size() == 1, "empty commit does not call transport");

        check(c.setPreedit(QStringLiteral("wor")), "setPreedit ok");
        check(c.preeditCount() == 1, "preeditCount == 1");
        check(mock.preedits.size() == 1 && mock.preedits[0] == QStringLiteral("wor"),
              "mock received preedit 'wor'");
        // Idempotent: same preedit not re-sent.
        check(c.setPreedit(QStringLiteral("wor")), "same preedit is no-op");
        check(mock.preedits.size() == 1, "idempotent preedit not re-sent");
        check(c.clearPreedit(), "clearPreedit ok");
        check(mock.preedits.size() == 2 && mock.preedits[1].isEmpty(),
              "clear sends empty preedit");
    }

    std::printf("[committer] skip when context becomes invalid\n");
    {
        MockContext mock;
        TextCommitter c;
        c.setContext(&mock);
        check(c.canCommit(), "initially can commit");
        mock.valid = false;
        // canCommit still true (we cache the pointer); transport isValid is checked
        // only via canCommit which calls isValid — so now it should be false.
        check(!c.canCommit(), "canCommit false after context invalidation");
        check(!c.commitText(QStringLiteral("x")), "commit skipped after invalidation");
        check(mock.commits.empty(), "no transport call after invalidation");
    }

    // Models InputMethodContext R2 behaviour: isValid is false until the
    // compositor's first commit_state (serial known). Mocks encode that as
    // valid=false until the test "receives" commit_state.
    std::printf("[committer] R2: skip commits before serial/commit_state ready\n");
    {
        MockContext mock;
        mock.valid = false; // no commit_state yet
        mock.ser = 0;
        TextCommitter c;
        c.setContext(&mock);
        check(!c.canCommit(), "cannot commit before commit_state");
        check(!c.commitText(QStringLiteral("early")), "pre-serial commit skipped");
        check(mock.commits.empty(), "no wire traffic before serial ready");
        check(c.skippedCount() >= 1, "skippedCount incremented");

        // First commit_state arrives (serial may still be 0 — that is OK).
        mock.valid = true;
        mock.ser = 0;
        check(c.canCommit(), "can commit after commit_state (serial 0 allowed)");
        check(c.commitText(QStringLiteral("ok")), "commit after serial ready");
        check(mock.commits.size() == 1 && mock.commits[0] == QStringLiteral("ok"),
              "commit delivered after serial ready");
    }

    std::printf("[committer] detach context on deactivate\n");
    {
        MockContext mock;
        TextCommitter c;
        c.setContext(&mock);
        check(c.canCommit(), "attached");
        c.setContext(nullptr);
        check(!c.canCommit(), "detached");
        check(!c.commitText(QStringLiteral("y")), "commit skipped after detach");
    }

    std::printf("[committer] long commit is chunked under Wayland size limit\n");
    {
        MockContext mock;
        TextCommitter c;
        c.setContext(&mock);
        // ASCII: UTF-8 size equals QString length — exceed the 4000-byte cap.
        const QString longText = QString(kWaylandCommitUtf8Limit + 500, QLatin1Char('a'));
        check(c.commitText(longText), "long commit ok");
        check(mock.commits.size() >= 2, "split into multiple wire commits");
        QString rejoined;
        int maxBytes = 0;
        for (const QString &chunk : mock.commits) {
            const int n = chunk.toUtf8().size();
            if (n > maxBytes) {
                maxBytes = n;
            }
            rejoined += chunk;
        }
        check(maxBytes <= kWaylandCommitUtf8Limit, "each chunk within limit");
        check(rejoined == longText, "chunks reassemble to original");
    }

    std::printf("[committer] splitForWaylandCommit helper\n");
    {
        const auto empty = splitForWaylandCommit(QString());
        check(empty.isEmpty(), "empty → no chunks");
        const auto one = splitForWaylandCommit(QStringLiteral("hi"));
        check(one.size() == 1 && one[0] == QStringLiteral("hi"), "short stays one chunk");
    }

    if (failures == 0) {
        std::printf("[committer] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[committer] %d TEST(S) FAILED\n", failures);
    return 1;
}
