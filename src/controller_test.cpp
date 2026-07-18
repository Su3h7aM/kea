/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-controller-test — DictationController state machine with injected fakes.
 *
 * Does not need a real GGUF, mic, or compositor. Covers:
 *   - missing-model path (production worker, no inject)
 *   - load → listen → drain → idle (fake worker + fake mic)
 *   - unloadModel while Draining is deferred until session finishes
 *   - start ignored while deferred unload is pending
 *   - cancel mid-session
 */
#include <cstdio>

#include <QCoreApplication>
#include <QEventLoop>
#include <QList>
#include <QMetaType>
#include <QTimer>

#include "app/app_settings.h"
#include "audio/audio_recorder.h"
#include "controller/dictation_controller.h"
#include "controller/inference_worker.h"

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

static void processFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Mic that always opens; tests can push PCM via emitPcm().
class FakeAudioRecorder : public AudioRecorder
{
public:
    explicit FakeAudioRecorder(QObject *parent = nullptr)
        : AudioRecorder(parent)
    {
    }

    bool start() override
    {
        m_active = true;
        setLastError(QString());
        return true;
    }

    void stop() override { m_active = false; }
    bool isActive() const override { return m_active; }

    void emitPcm(const QList<float> &samples) { Q_EMIT pcmBlock(samples); }
    void emitLevel(float l) { Q_EMIT levelChanged(l); }

private:
    bool m_active = false;
};

/// Deterministic InferenceWorker for controller tests.
class FakeWorker : public InferenceWorker
{
    Q_OBJECT
public:
    explicit FakeWorker(QObject *parent = nullptr)
        : InferenceWorker(parent)
    {
    }

    bool modelOk = false;
    bool offlineMode = true;
    bool sessionActive = false;
    bool unloadCalled = false;
    int loadCount = 0;
    int beginCount = 0;
    int finalizeCount = 0;
    int cancelCount = 0;
    int feedCount = 0;
    QString finishText = QStringLiteral("hello world");
    QString finishError;
    QList<float> buffered;

public Q_SLOTS:
    void loadBackend(int /*device*/, const QString & /*modelPath*/) override
    {
        ++loadCount;
        modelOk = true;
        Q_EMIT modelReady(true, QString());
    }

    void unloadBackend() override
    {
        unloadCalled = true;
        modelOk = false;
        sessionActive = false;
        buffered.clear();
        Q_EMIT modelUnloaded();
    }

    void beginSession() override
    {
        ++beginCount;
        if (!modelOk) {
            Q_EMIT sessionStarted(false, QStringLiteral("model not loaded"), false);
            return;
        }
        sessionActive = true;
        buffered.clear();
        Q_EMIT sessionStarted(true, QString(), offlineMode);
    }

    void feedPcm(const QList<float> &samples) override
    {
        if (!sessionActive) {
            return;
        }
        ++feedCount;
        if (offlineMode) {
            buffered.append(samples);
        } else {
            Q_EMIT textFinalized(QStringLiteral("partial"), 0);
        }
    }

    void finalizeSession() override
    {
        ++finalizeCount;
        if (!sessionActive) {
            Q_EMIT sessionFinished(QString(), QStringLiteral("no active session"));
            return;
        }
        sessionActive = false;
        Q_EMIT sessionFinished(finishText, finishError);
    }

    void cancelSession() override
    {
        ++cancelCount;
        sessionActive = false;
        buffered.clear();
        Q_EMIT sessionCancelled();
    }
};

int main(int argc, char *argv[])
{
    qRegisterMetaType<QList<float>>("QList<float>");
    // Never write unit-test settings into the user's ~/.config/kea/kea.conf.
    qputenv("KEA_SETTINGS_FILE", QByteArray("/tmp/kea-controller-test-settings.ini"));
    QCoreApplication app(argc, argv);

    std::printf("[controller] initial state (production ctor)\n");
    {
        AppSettings settings;
        DictationController c(&settings);
        check(c.state() == DictationController::State::Idle, "starts Idle");
        check(!c.isListening(), "not listening");
        c.cancel();
        check(c.state() == DictationController::State::Idle, "cancel idle no-op");
    }

    std::printf("[controller] start without model file (production worker)\n");
    {
        AppSettings settings;
        DictationController c(&settings);
        // Skip when a real model is present — would open the production worker
        // against a real GGUF (slow / environment-dependent).
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
            std::printf("  skip (model file present — covered by fake-worker tests)\n");
        }
    }

    std::printf("[controller] fake: load → listen → stop → idle\n");
    {
        AppSettings settings;
        FakeWorker worker;
        FakeAudioRecorder recorder;
        DictationController c(&settings, &worker, &recorder);

        c.loadModel();
        processFor(20);
        check(c.isModelLoaded(), "model loaded via fake");
        check(c.state() == DictationController::State::Idle, "ready idle");
        check(worker.loadCount == 1, "loadBackend once");

        c.start();
        check(c.state() == DictationController::State::Starting, "Starting sync");
        processFor(20);
        check(c.state() == DictationController::State::Listening, "Listening");
        check(recorder.isActive(), "mic open");
        check(worker.beginCount == 1, "beginSession once");
        check(worker.offlineMode, "offline session");

        recorder.emitPcm(QList<float>{0.1f, 0.2f, 0.3f});
        processFor(20);
        check(worker.feedCount == 1, "PCM fed to worker");
        check(worker.buffered.size() == 3, "offline buffer has samples");

        c.stop();
        check(c.state() == DictationController::State::Draining, "Draining");
        check(!recorder.isActive(), "mic stopped on drain");
        processFor(20);
        check(c.state() == DictationController::State::Idle, "back to Idle");
        check(worker.finalizeCount == 1, "finalize once");
    }

    std::printf("[controller] unloadModel while Draining is deferred\n");
    {
        AppSettings settings;
        FakeWorker worker;
        FakeAudioRecorder recorder;
        DictationController c(&settings, &worker, &recorder);

        c.loadModel();
        processFor(20);
        c.start();
        processFor(20);
        check(c.state() == DictationController::State::Listening, "listening before stop");

        c.stop();
        check(c.state() == DictationController::State::Draining, "draining");
        check(c.isModelLoaded(), "still loaded during drain");

        c.unloadModel(); // must defer
        check(c.isModelLoaded(), "still loaded after deferred unload request");
        check(!worker.unloadCalled, "unloadBackend not yet called");
        check(c.statusText().contains(QStringLiteral("unload")),
              "status mentions deferred unload");

        // start() should be ignored while unload is pending after drain
        c.start();
        check(c.state() == DictationController::State::Draining,
              "start ignored while deferred unload pending");

        processFor(20); // finalize → maybeUnloadAfterDrain
        check(c.state() == DictationController::State::Idle
                  || c.state() == DictationController::State::Error,
              "session resolved");
        processFor(20); // queued unloadBackend
        check(worker.unloadCalled, "unloadBackend ran after drain");
        check(!c.isModelLoaded(), "model unloaded after drain");
    }

    std::printf("[controller] cancel mid listen\n");
    {
        AppSettings settings;
        FakeWorker worker;
        FakeAudioRecorder recorder;
        DictationController c(&settings, &worker, &recorder);

        c.loadModel();
        processFor(20);
        c.start();
        processFor(20);
        check(c.state() == DictationController::State::Listening, "listening");
        c.cancel();
        check(c.state() == DictationController::State::Idle, "cancelled → Idle");
        processFor(20);
        check(worker.cancelCount == 1, "cancelSession invoked");
        check(!recorder.isActive(), "mic stopped");
    }

    std::printf("[controller] release while Starting cancels without listen\n");
    {
        AppSettings settings;
        FakeWorker worker;
        FakeAudioRecorder recorder;
        // Delay sessionStarted by not auto-emitting — call manually after stop.
        // Override: use a worker that doesn't auto-complete begin until we say.
        // FakeWorker emits immediately — so we stop before processEvents.
        DictationController c(&settings, &worker, &recorder);
        c.loadModel();
        processFor(20);
        c.start();
        check(c.state() == DictationController::State::Starting, "Starting");
        c.stop(); // m_stopWhenStarted
        check(c.statusText().contains(QStringLiteral("Cancell")), "cancelling status");
        processFor(20);
        // sessionStarted runs with stopWhenStarted → cancel, Idle
        check(c.state() == DictationController::State::Idle, "never entered Listening");
        check(worker.cancelCount >= 1, "session cancelled");
        check(!recorder.isActive(), "mic never left open");
    }

    if (failures == 0) {
        std::printf("[controller] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[controller] %d TEST(S) FAILED\n", failures);
    return 1;
}

#include "controller_test.moc"
