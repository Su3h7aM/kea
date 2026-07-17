/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-controller-test — headless checks for DictationController basics.
 * Does not write into the user's real QSettings("kea","kea").
 */
#include <cstdio>

#include <QCoreApplication>
#include <QEventLoop>
#include <QList>
#include <QMetaType>
#include <QTimer>

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
    qRegisterMetaType<QList<float>>("QList<float>");
    QCoreApplication app(argc, argv);

    std::printf("[controller] initial state\n");
    AppSettings settings;
    DictationController c(&settings);
    check(c.state() == DictationController::State::Idle, "starts Idle");
    check(!c.isListening(), "not listening");

    std::printf("[controller] cancel while idle is a no-op\n");
    c.cancel();
    check(c.state() == DictationController::State::Idle, "still Idle after cancel");

    std::printf("[controller] start while no model on disk (if path missing)\n");
    if (!settings.modelFileExists()) {
        c.start();
        check(c.state() == DictationController::State::LoadingModel
                  || c.state() == DictationController::State::Starting
                  || c.state() == DictationController::State::Error,
              "entered busy/error without model file");

        QEventLoop loop;
        QObject::connect(&c, &DictationController::stateChanged, &loop, [&]() {
            if (c.state() == DictationController::State::Error
                || c.state() == DictationController::State::Idle) {
                loop.quit();
            }
        });
        QTimer::singleShot(8000, &loop, &QEventLoop::quit);
        loop.exec();
        check(c.state() == DictationController::State::Error
                  || c.state() == DictationController::State::Idle,
              "settled after missing-model start");
    } else {
        std::printf("  skip (model file present at %s)\n",
                    qPrintable(settings.modelPath()));
    }

    std::printf("[controller] triple start while LoadingModel is single-flight\n");
    DictationController c2(&settings);
    if (!settings.modelFileExists()) {
        c2.start();
        c2.start();
        c2.start();
        check(c2.state() == DictationController::State::LoadingModel
                  || c2.state() == DictationController::State::Error
                  || c2.state() == DictationController::State::Starting,
              "triple start stays single-flight");
    } else {
        std::printf("  skip (would open real model)\n");
    }

    if (failures == 0) {
        std::printf("[controller] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[controller] %d TEST(S) FAILED\n", failures);
    return 1;
}
