/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-parakeet-backend-test — headless checks for ParakeetBackend's dlopen
 * loader, model-load error handling, and context-handle lifetime.
 *
 *   1. Initial state before any load() call.
 *   2. loadModel() before load() fails cleanly ("backend not loaded").
 *   3. load() resolves and dlopens the fake C-API library.
 *   4. loadModel() failure (missing gguf path) reports the new, simpler
 *      error message when capi_load() returns a null context.
 *   5. loadModel() success for an existing (fake) model file.
 *   6. unload() is idempotent.
 *   7. Repeated construct/load/loadModel/destroy cycles don't crash —
 *      regression test for the ~ParakeetBackend() context-handle leak fix.
 *
 * Uses kea-fake-parakeet-capi (see inference/fake_parakeet_capi.cpp), a tiny
 * stand-in .so exporting the same flat C-API surface as the real
 * parakeet.cpp, so the loader can be exercised without fetching/building the
 * real dependency. Its capi_last_error() follows upstream by returning an
 * empty string for a null context.
 *
 * Run under ASan/LeakSanitizer to also catch the context-handle leak this PR
 * fixes in ~ParakeetBackend().
 *
 * Returns 0 on success, 1 on any failure.
 */
#include <cstdio>

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QTemporaryFile>

#include "inference/parakeet_backend.h"

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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Point the loader at the fake .so built alongside this test.
    // KEA_FAKE_PARAKEET_LIB is baked in by CMakeLists.txt as the fake
    // library's built target file path.
    qputenv("KEA_PARAKEET_LIB", QByteArray(KEA_FAKE_PARAKEET_LIB));

    std::printf("[parakeet_backend] initial state before load()\n");
    {
        ParakeetBackend b;
        check(!b.isLoaded(), "not loaded initially");
        check(b.lastError().isEmpty(), "no error initially");
        check(!b.hasStream(), "no stream initially");
    }

    std::printf("[parakeet_backend] loadModel() before load() fails cleanly\n");
    {
        ParakeetBackend b;
        check(!b.loadModel(QStringLiteral("/nonexistent/model.gguf")),
              "loadModel fails when backend not loaded");
        check(b.lastError() == QStringLiteral("backend not loaded"),
              "error is 'backend not loaded'");
    }

    ParakeetBackend b;

    std::printf("[parakeet_backend] load() resolves and dlopens the fake library\n");
    {
        const bool loaded = b.load(ParakeetDevice::Cpu);
        if (!loaded) {
            std::printf("    lastError: %s\n", qPrintable(b.lastError()));
        }
        check(loaded, "load(Cpu) succeeds against the fake .so");
        check(b.isLoaded(), "isLoaded() true after load()");
    }

    std::printf("[parakeet_backend] loadModel() failure: exact message, no crash calling into capi_last_error\n");
    {
        const QString missingPath = QStringLiteral("/nonexistent/kea-test-model.gguf");
        const bool ok = b.loadModel(missingPath);
        check(!ok, "loadModel fails for a nonexistent gguf path");
        const QString expected =
            QStringLiteral("parakeet_capi_load failed for %1").arg(missingPath);
        check(b.lastError() == expected,
              "error message matches the new format exactly (no ': <capi_last_error>' suffix)");
    }

    std::printf("[parakeet_backend] loadModel() success for an existing (fake) model file\n");
    {
        QTemporaryFile tmp;
        check(tmp.open(), "temp model file opens");
        tmp.write("fake gguf contents");
        tmp.flush();
        const bool ok = b.loadModel(tmp.fileName());
        check(ok, "loadModel succeeds for an existing file");
    }

    std::printf("[parakeet_backend] loadModel() can be called again to swap models\n");
    {
        QTemporaryFile tmp2;
        check(tmp2.open(), "second temp model file opens");
        tmp2.write("other fake gguf contents");
        tmp2.flush();
        // Exercises the branch that frees the previously loaded context
        // (m_ctx->ctx) before loading the new one.
        check(b.loadModel(tmp2.fileName()), "loadModel succeeds again, replacing the prior context");
    }

    std::printf("[parakeet_backend] unload() is idempotent\n");
    {
        b.unload();
        check(!b.isLoaded(), "unload() clears isLoaded()");
        b.unload(); // second call must not crash / double-free
        check(!b.isLoaded(), "second unload() is a safe no-op");
    }

    std::printf("[parakeet_backend] repeated construct/load/loadModel/destroy does not crash\n");
    {
        QTemporaryFile tmp;
        check(tmp.open(), "temp model file opens for lifetime loop");
        tmp.write("fake gguf contents");
        tmp.flush();
        const QString path = tmp.fileName();

        for (int i = 0; i < 200; ++i) {
            auto *heapBackend = new ParakeetBackend();
            heapBackend->load(ParakeetDevice::Cpu);
            heapBackend->loadModel(path);
            delete heapBackend; // exercises ~ParakeetBackend()'s delete m_ctx fix
        }
        check(true, "200 construct/load/loadModel/destroy cycles completed without crashing");
    }

    if (failures == 0) {
        std::printf("[parakeet_backend] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[parakeet_backend] %d TEST(S) FAILED\n", failures);
    return 1;
}
