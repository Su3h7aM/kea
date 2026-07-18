/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-transform-test — post-process pipeline (issue #6) without llama.cpp.
 *
 * Covers style prompts, FakeTransformer, and controller gated path:
 *   preedit buffer while speaking → transform on stop → commit result.
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
#include "insert/input_context.h"
#include "insert/insertion_router.h"
#include "insert/text_committer.h"
#include "transform/text_transformer.h"

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
        return true;
    }
    void stop() override { m_active = false; }
    bool isActive() const override { return m_active; }
    void emitPcm(const QList<float> &s) { Q_EMIT pcmBlock(s); }

private:
    bool m_active = false;
};

class FakeWorker : public InferenceWorker
{
    Q_OBJECT
public:
    explicit FakeWorker(QObject *parent = nullptr)
        : InferenceWorker(parent)
    {
    }
    QString finishText = QStringLiteral("hello world");

public Q_SLOTS:
    void loadBackend(int, const QString &) override { Q_EMIT modelReady(true, {}); }
    void unloadBackend() override { Q_EMIT modelUnloaded(); }
    void beginSession() override { Q_EMIT sessionStarted(true, {}, true); }
    void feedPcm(const QList<float> &) override {}
    void finalizeSession() override { Q_EMIT sessionFinished(finishText, {}); }
    void cancelSession() override { Q_EMIT sessionCancelled(); }
};

class MockContext : public IInputContext
{
public:
    bool valid = true;
    uint32_t ser = 1;
    QStringList commits;
    QStringList preedits;
    bool isValid() const override { return valid; }
    uint32_t serial() const override { return ser; }
    void commitString(const QString &t) override { commits.append(t); }
    void setPreedit(const QString &t, const QString &) override { preedits.append(t); }
};

int main(int argc, char *argv[])
{
    qRegisterMetaType<QList<float>>("QList<float>");
    QCoreApplication app(argc, argv);

    std::printf("[transform] style helpers\n");
    {
        check(TextTransformer::styleName(TransformStyle::Correct)
                  == QStringLiteral("Correct"),
              "Correct name");
        check(!TextTransformer::stylePrompt(TransformStyle::Correct).isEmpty(),
              "Correct prompt non-empty");
        check(TextTransformer::styleFromInt(99) == TransformStyle::Correct,
              "invalid style → Correct");
        check(TextTransformer::styleFromInt(2) == TransformStyle::Professional,
              "style 2 professional");
    }

    std::printf("[transform] FakeTransformer\n");
    {
        FakeTransformer f;
        check(!f.isReady(), "not ready before load");
        check(f.loadModel(QStringLiteral("/tmp/x.gguf")), "load ok");
        check(f.isReady(), "ready after load");
        const QString out = f.transform(QStringLiteral("hi"), TransformStyle::Enhance);
        check(out == QStringLiteral("[fixed] hi"), "prefix applied");
        check(f.lastStyle == TransformStyle::Enhance, "style recorded");
        check(f.callCount == 1, "call count");
    }

    std::printf("[transform] controller gated path uses preedit then polish\n");
    {
        AppSettings settings;
        settings.setPostProcessEnabled(true);
        settings.setPostProcessStyle(0); // Correct
        // llm path existence not required for FakeTransformer readiness
        settings.setLlmModelPath(QStringLiteral("/tmp/kea-fake-llm.gguf"));

        FakeWorker worker;
        FakeAudioRecorder recorder;
        FakeTransformer transformer;
        transformer.loadModel(QStringLiteral("/tmp/kea-fake-llm.gguf"));

        TextCommitter committer;
        MockContext ctx;
        committer.setContext(&ctx);
        InsertionRouter router;
        router.setTextCommitter(&committer);

        DictationController c(&settings, &worker, &recorder, &transformer);
        c.setInsertionRouter(&router);

        c.loadModel();
        processFor(30);
        check(c.isModelLoaded(), "ASR model loaded");

        c.start();
        processFor(30);
        check(c.state() == DictationController::State::Listening, "listening");

        // Offline session: no mid-stream textFinalized; finalize delivers all.
        worker.finishText = QStringLiteral("helo wrld");
        c.stop();
        processFor(30); // finalize → queueTransform
        processFor(50); // transform → deliver

        check(transformer.callCount >= 1, "transformer invoked");
        check(transformer.lastInput.contains(QStringLiteral("helo wrld")),
              "raw ASR sent to transformer");
        processFor(30); // queued insert
        check(!ctx.commits.isEmpty(), "commit happened");
        check(ctx.commits.last().startsWith(QStringLiteral("[fixed]")),
              "committed polished text");
    }

    std::printf("[transform] verbatim path does not call transformer\n");
    {
        AppSettings settings;
        settings.setPostProcessEnabled(false);

        FakeWorker worker;
        FakeAudioRecorder recorder;
        FakeTransformer transformer;
        transformer.loadModel(QStringLiteral("/tmp/x.gguf"));

        TextCommitter committer;
        MockContext ctx;
        committer.setContext(&ctx);
        InsertionRouter router;
        router.setTextCommitter(&committer);

        DictationController c(&settings, &worker, &recorder, &transformer);
        c.setInsertionRouter(&router);
        c.loadModel();
        processFor(20);
        c.start();
        processFor(20);
        worker.finishText = QStringLiteral("raw only");
        c.stop();
        processFor(40);
        check(transformer.callCount == 0, "transformer not called when disabled");
        processFor(20);
        check(!ctx.commits.isEmpty() && ctx.commits.last() == QStringLiteral("raw only"),
              "raw committed");
    }

    if (failures == 0) {
        std::printf("[transform] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[transform] %d TEST(S) FAILED\n", failures);
    return 1;
}

#include "transform_test.moc"
