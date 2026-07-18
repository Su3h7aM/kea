/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Headless tests for InsertionRouter (IM → fake_input → clipboard).
 * Without a compositor, fake_input is unavailable; clipboard still works.
 */
#include <cstdio>
#include <cstdlib>

#include <QClipboard>
#include <QGuiApplication>

#include "insert/input_context.h"
#include "insert/insertion_router.h"
#include "insert/text_committer.h"

using kea::IInputContext;
using kea::InsertionRouter;
using kea::TextCommitter;

static int g_fails = 0;

static void check(bool cond, const char *msg)
{
    if (cond) {
        std::printf("  ok   %s\n", msg);
    } else {
        std::printf("  FAIL %s\n", msg);
        ++g_fails;
    }
}

class MockContext : public IInputContext
{
public:
    bool valid = true;
    QStringList commits;

    bool isValid() const override { return valid; }
    uint32_t serial() const override { return 1; }
    void commitString(const QString &text) override { commits.push_back(text); }
    void setPreedit(const QString & /*text*/, const QString & /*fb*/) override {}
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    std::printf("[router] IM commit when context live\n");
    {
        TextCommitter c;
        MockContext mock;
        c.setContext(&mock);
        InsertionRouter r;
        r.setTextCommitter(&c);
        const auto res = r.insertText(QStringLiteral("hello"));
        check(res.delivered, "delivered");
        check(res.path == InsertionRouter::Path::InputMethod, "path=IM");
        check(mock.commits.size() == 1 && mock.commits[0] == QStringLiteral("hello"),
              "mock got hello");
    }

    std::printf("[router] clipboard when IM and fake_input unavailable\n");
    {
        TextCommitter c;
        InsertionRouter r;
        r.setTextCommitter(&c);
        // Offscreen / no KWin: fake_input not bound → clipboard.
        const auto res = r.insertText(QStringLiteral("from-mic"));
        check(res.delivered, "delivered via clipboard");
        check(res.path == InsertionRouter::Path::Clipboard, "path=clipboard last resort");
        check(QGuiApplication::clipboard()->text() == QStringLiteral("from-mic"),
              "clipboard has text");
        check(!res.detail.isEmpty(), "status detail non-empty");
    }

    std::printf("[router] IM preferred over later paths when both available\n");
    {
        TextCommitter c;
        MockContext mock;
        c.setContext(&mock);
        InsertionRouter r;
        r.setTextCommitter(&c);
        QGuiApplication::clipboard()->clear();
        const auto res = r.insertText(QStringLiteral("prefer-im"));
        check(res.path == InsertionRouter::Path::InputMethod, "uses IM first");
        check(mock.commits.size() == 1, "IM commit happened");
        check(res.path != InsertionRouter::Path::Clipboard, "not clipboard");
        check(res.path != InsertionRouter::Path::FakeInput, "not fake_input");
    }

    std::printf("[router] empty text is success no-op\n");
    {
        InsertionRouter r;
        const auto res = r.insertText(QString());
        check(res.delivered, "empty delivered");
        check(res.path == InsertionRouter::Path::None, "path=none");
    }

    if (g_fails) {
        std::printf("[router] %d FAILURES\n", g_fails);
        return 1;
    }
    std::printf("[router] ALL TESTS PASSED\n");
    return 0;
}
