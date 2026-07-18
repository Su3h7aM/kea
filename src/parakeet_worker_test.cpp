/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-parakeet-worker-test — ParakeetWorker against kea-fake-parakeet-capi.
 *
 * Covers offline (default fake) and streaming (KEA_FAKE_STREAMING=1) paths:
 * beginSession auto-detect, feedPcm buffering vs live finalize, cancel.
 */
#include <cstdio>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QList>
#include <QMetaType>
#include <QTemporaryFile>
#include <QTimer>

#include "controller/parakeet_worker.h"

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

static QString makeTempModelFile()
{
    QTemporaryFile f(QStringLiteral("kea-fake-model-XXXXXX.gguf"));
    f.setAutoRemove(false);
    f.open();
    f.write("GGUF");
    f.write(QByteArray(2048, 'm'));
    f.close();
    return f.fileName();
}

static void pump(int ms = 50)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char *argv[])
{
    qRegisterMetaType<QList<float>>("QList<float>");
    QCoreApplication app(argc, argv);

    qputenv("KEA_PARAKEET_LIB", QByteArray(KEA_FAKE_PARAKEET_LIB));
    qputenv("KEA_PARAKEET_CPU_LIB", QByteArray(KEA_FAKE_PARAKEET_LIB));
    qputenv("KEA_PARAKEET_VULKAN_LIB", QByteArray(KEA_FAKE_PARAKEET_LIB));
    qunsetenv("KEA_FAKE_STREAMING");

    const QString modelPath = makeTempModelFile();

    std::printf("[worker] offline: load → begin → feed → finalize\n");
    {
        ParakeetWorker w;
        bool ready = false;
        bool offline = false;
        QString finishedText;
        QObject::connect(&w, &ParakeetWorker::modelReady, &app,
                         [&](bool ok, const QString &err) {
                             ready = ok;
                             check(ok, "modelReady ok");
                             if (!ok) {
                                 std::printf("    err: %s\n", qPrintable(err));
                             }
                         });
        QObject::connect(&w, &ParakeetWorker::sessionStarted, &app,
                         [&](bool ok, const QString &, bool off) {
                             check(ok, "sessionStarted ok");
                             offline = off;
                         });
        QObject::connect(&w, &ParakeetWorker::sessionFinished, &app,
                         [&](const QString &text, const QString &err) {
                             finishedText = text;
                             check(err.isEmpty(), "sessionFinished no error");
                         });

        w.loadBackend(0, modelPath);
        check(ready, "offline model loaded");

        w.beginSession();
        check(offline, "offline mode (stream_begin fails on fake by default)");

        QList<float> pcm;
        pcm.reserve(160);
        for (int i = 0; i < 160; ++i) {
            pcm.append(0.01f);
        }
        w.feedPcm(pcm);
        w.feedPcm(pcm);
        w.finalizeSession();
        check(finishedText == QStringLiteral("fake-offline-transcript"),
              "offline transcript from fake transcribe_pcm");
    }

    std::printf("[worker] cancel mid offline session\n");
    {
        ParakeetWorker w;
        bool cancelled = false;
        QObject::connect(&w, &ParakeetWorker::sessionCancelled, &app, [&]() {
            cancelled = true;
        });
        w.loadBackend(0, modelPath);
        w.beginSession();
        w.feedPcm(QList<float>{0.1f, 0.2f});
        w.cancelSession();
        check(cancelled, "sessionCancelled emitted");
        // finalize after cancel should report no active session
        QString err;
        QObject::connect(&w, &ParakeetWorker::sessionFinished, &app,
                         [&](const QString &, const QString &e) { err = e; });
        w.finalizeSession();
        check(err.contains(QStringLiteral("no active session")),
              "finalize after cancel reports no session");
    }

    std::printf("[worker] streaming path (KEA_FAKE_STREAMING=1)\n");
    {
        qputenv("KEA_FAKE_STREAMING", QByteArrayLiteral("1"));
        // Stream symbols are resolved at load() time — new backend needed.
        ParakeetWorker w;
        bool offline = true;
        QString partial;
        QString finalText;
        QObject::connect(&w, &ParakeetWorker::sessionStarted, &app,
                         [&](bool ok, const QString &, bool off) {
                             check(ok, "streaming sessionStarted");
                             offline = off;
                         });
        QObject::connect(&w, &ParakeetWorker::textFinalized, &app,
                         [&](const QString &t, int) { partial = t; });
        QObject::connect(&w, &ParakeetWorker::sessionFinished, &app,
                         [&](const QString &t, const QString &e) {
                             finalText = t;
                             check(e.isEmpty(), "streaming finalize no error");
                         });

        w.loadBackend(0, modelPath);
        w.beginSession();
        check(!offline, "streaming mode when fake streaming enabled");

        QList<float> pcm{0.1f, 0.2f, 0.3f};
        w.feedPcm(pcm);
        check(partial == QStringLiteral("fake-stream-partial"),
              "textFinalized on first feed");
        w.finalizeSession();
        check(finalText == QStringLiteral("fake-stream-final"),
              "streaming finalize text");
        qunsetenv("KEA_FAKE_STREAMING");
    }

    std::printf("[worker] load missing model fails cleanly\n");
    {
        ParakeetWorker w;
        bool ok = true;
        QString err;
        QObject::connect(&w, &ParakeetWorker::modelReady, &app,
                         [&](bool o, const QString &e) {
                             ok = o;
                             err = e;
                         });
        w.loadBackend(0, QStringLiteral("/nonexistent/kea-no-model.gguf"));
        check(!ok, "modelReady false for missing file");
        check(!err.isEmpty(), "error message set");
    }

    Q_UNUSED(pump);

    if (failures == 0) {
        std::printf("[worker] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[worker] %d TEST(S) FAILED\n", failures);
    return 1;
}
