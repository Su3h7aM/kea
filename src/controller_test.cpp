/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-controller-test — headless checks for DictationController basics.
 *
 * Does not require a microphone or model. Verifies initial state, cancel-on-idle,
 * and that start() without a model enters LoadingModel then Error (missing GGUF).
 */
#include <cstdio>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVector>

#include "app/app_settings.h"
#include "controller/dictation_controller.h"

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
    qRegisterMetaType<QVector<float>>("QVector<float>");
    QCoreApplication app(argc, argv);

    std::printf("[controller] initial state\n");
    AppSettings settings;
    // Point at a path that cannot exist so load fails deterministically.
    settings.setModelPath(QStringLiteral("/tmp/kea-definitely-missing-model.gguf"));

    DictationController c(&settings);
    check(c.state() == DictationController::State::Idle, "starts Idle");
    check(!c.isListening(), "not listening");
    check(!c.isModelLoaded(), "model not loaded");

    std::printf("[controller] cancel while idle is a no-op\n");
    c.cancel();
    check(c.state() == DictationController::State::Idle, "still Idle after cancel");

    std::printf("[controller] start without model → LoadingModel → Error\n");
    c.start();
    check(c.state() == DictationController::State::LoadingModel
              || c.state() == DictationController::State::Error,
          "entered LoadingModel (or Error if already failed)");

    // Pump the event loop so the worker's queued modelReady can arrive.
    QEventLoop loop;
    QObject::connect(&c, &DictationController::stateChanged, &loop, [&]() {
        if (c.state() == DictationController::State::Error
            || c.state() == DictationController::State::Idle) {
            loop.quit();
        }
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    check(c.state() == DictationController::State::Error, "ends in Error (missing model)");
    check(!c.lastError().isEmpty(), "lastError is set");

    if (failures == 0) {
        std::printf("[controller] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[controller] %d TEST(S) FAILED\n", failures);
    return 1;
}
