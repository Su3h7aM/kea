/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Headless tests for InsertionRouter (IM + clipboard fallback).
 * Uses QGuiApplication for QClipboard; no compositor required.
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
        r.setClipboardFallbackEnabled(true);
        const auto res = r.insertText(QStringLiteral("hello"));
        check(res.delivered, "delivered");
        check(res.path == InsertionRouter::Path::InputMethod, "path=IM");
        check(mock.commits.size() == 1 && mock.commits[0] == QStringLiteral("hello"),
              "mock got hello");
    }

    std::printf("[router] clipboard fallback when no context\n");
    {
        TextCommitter c;
        InsertionRouter r;
        r.setTextCommitter(&c);
        r.setClipboardFallbackEnabled(true);
        const auto res = r.insertText(QStringLiteral("from-mic"));
        check(res.delivered, "delivered via clipboard");
        check(res.path == InsertionRouter::Path::Clipboard, "path=clipboard");
        check(QGuiApplication::clipboard()->text() == QStringLiteral("from-mic"),
              "clipboard has text");
        check(!res.detail.isEmpty(), "status detail non-empty");
    }

    std::printf("[router] fail hard when fallback disabled and no context\n");
    {
        TextCommitter c;
        InsertionRouter r;
        r.setTextCommitter(&c);
        r.setClipboardFallbackEnabled(false);
        const auto res = r.insertText(QStringLiteral("lost"));
        check(!res.delivered, "not delivered");
        check(res.path == InsertionRouter::Path::None, "path=none");
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
