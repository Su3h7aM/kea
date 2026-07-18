/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-parakeet-backend-test — headless checks for ParakeetBackend's dlopen
 * loader, model-load error handling, same-device reuse, and runtime
 * CPU ↔ Vulkan switch (full unload then reload).
 *
 *   1. Initial state before any load() call.
 *   2. loadModel() before load() fails cleanly ("backend not loaded").
 *   3. load() resolves and dlopens the fake C-API library.
 *   4. loadModel() failure reports a clean error; hasModel() stays false.
 *   5. loadModel() success / swap for existing (fake) model files.
 *   6. unload() clears isLoaded / hasModel (full dlclose).
 *   7. load() same device after unload works again.
 *   8. load() different device switches in-process (no app restart).
 *   9. Same-device load() while already mapped reuses the handle.
 *  10. Repeated construct/load/loadModel/destroy cycles don't crash.
 *
 * Uses kea-fake-parakeet-capi (see inference/fake_parakeet_capi.cpp).
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
    qputenv("KEA_PARAKEET_LIB", QByteArray(KEA_FAKE_PARAKEET_LIB));
    // Same path for both variants so the test exercises device tracking and
    // switch logic rather than path resolution differences.
    qputenv("KEA_PARAKEET_CPU_LIB", QByteArray(KEA_FAKE_PARAKEET_LIB));
    qputenv("KEA_PARAKEET_VULKAN_LIB", QByteArray(KEA_FAKE_PARAKEET_LIB));

    std::printf("[parakeet_backend] initial state before load()\n");
    {
        ParakeetBackend b;
        check(!b.isLoaded(), "not loaded initially");
        check(!b.hasModel(), "no model initially");
        check(b.lastError().isEmpty(), "no error initially");
        check(!b.hasStream(), "no stream initially");
        check(!b.loadedDevice().has_value(), "no loadedDevice initially");
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
        check(b.loadedDevice().has_value() && *b.loadedDevice() == ParakeetDevice::Cpu,
              "loadedDevice() is Cpu");
    }

    std::printf("[parakeet_backend] same-device load() reuses mapped library\n");
    {
        check(b.load(ParakeetDevice::Cpu), "second load(Cpu) succeeds without unload cycle");
        check(b.isLoaded(), "still loaded after same-device load()");
        check(b.loadedDevice().has_value() && *b.loadedDevice() == ParakeetDevice::Cpu,
              "loadedDevice() still Cpu");
    }

    std::printf("[parakeet_backend] loadModel() failure: exact message, hasModel false\n");
    {
        const QString missingPath = QStringLiteral("/nonexistent/kea-test-model.gguf");
        const bool ok = b.loadModel(missingPath);
        check(!ok, "loadModel fails for a nonexistent gguf path");
        const QString expected =
            QStringLiteral("parakeet_capi_load failed for %1").arg(missingPath);
        check(b.lastError() == expected,
              "error message matches the new format exactly (no ': <capi_last_error>' suffix)");
        check(!b.hasModel(), "hasModel() false after failed loadModel");
    }

    std::printf("[parakeet_backend] loadModel() success for an existing (fake) model file\n");
    {
        QTemporaryFile tmp;
        check(tmp.open(), "temp model file opens");
        tmp.write("fake gguf contents");
        tmp.flush();
        const bool ok = b.loadModel(tmp.fileName());
        check(ok, "loadModel succeeds for an existing file");
        check(b.hasModel(), "hasModel() true after successful loadModel");
    }

    std::printf("[parakeet_backend] loadModel() can be called again to swap models\n");
    {
        QTemporaryFile tmp2;
        check(tmp2.open(), "second temp model file opens");
        tmp2.write("other fake gguf contents");
        tmp2.flush();
        check(b.loadModel(tmp2.fileName()), "loadModel succeeds again, replacing the prior context");
        check(b.hasModel(), "hasModel() still true after swap");
    }

    std::printf("[parakeet_backend] unload() is full teardown and idempotent\n");
    {
        b.unload();
        check(!b.isLoaded(), "unload() clears isLoaded()");
        check(!b.hasModel(), "unload() clears hasModel()");
        check(!b.loadedDevice().has_value(), "unload() clears loadedDevice()");
        b.unload(); // second call must not crash / double-free
        check(!b.isLoaded(), "second unload() is a safe no-op");
    }

    std::printf("[parakeet_backend] load() same device after unload works again\n");
    {
        check(b.load(ParakeetDevice::Cpu), "load(Cpu) after unload succeeds");
        check(b.isLoaded(), "loaded after re-open");
    }

    std::printf("[parakeet_backend] load() different device switches in-process\n");
    {
        check(b.load(ParakeetDevice::Vulkan),
              "load(Vulkan) after CPU succeeds (runtime switch, no restart)");
        check(b.isLoaded(), "loaded after switch");
        check(b.loadedDevice().has_value() && *b.loadedDevice() == ParakeetDevice::Vulkan,
              "loadedDevice() is Vulkan after switch");
        check(b.load(ParakeetDevice::Cpu),
              "load(Cpu) after Vulkan switches back");
        check(b.loadedDevice().has_value() && *b.loadedDevice() == ParakeetDevice::Cpu,
              "loadedDevice() is Cpu after switch back");
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
            delete heapBackend;
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
