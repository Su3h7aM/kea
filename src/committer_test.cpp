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

    if (failures == 0) {
        std::printf("[committer] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[committer] %d TEST(S) FAILED\n", failures);
    return 1;
}
