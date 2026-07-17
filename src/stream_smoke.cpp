/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-stream-smoke — Phase 1 end-to-end check of the streaming pipeline.
 *
 * Loads a WAV, resamples it to 16 kHz mono float, opens a parakeet streaming
 * session, feeds the audio in fixed chunks, and prints finalized text as it
 * arrives with [EOU]/[EOB] markers — exactly mirroring `parakeet-cli --stream`.
 *
 * This exercises the resampler + the streaming backend without a microphone, so
 * it can run anywhere the model + a backend .so are available:
 *
 *   kea-stream-smoke <cpu|vulkan> <model.gguf> <audio.wav> [chunkMs]
 */
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVector>
#include <chrono>
#include <iostream>

#include "audio/resampler.h"
#include "audio/wav_loader.h"
#include "inference/parakeet_backend.h"

using namespace kea;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        std::cerr << "usage: kea-stream-smoke <cpu|vulkan> <model.gguf> <audio.wav> [chunkMs]\n";
        return 2;
    }

    const ParakeetDevice device =
        args.at(1) == QStringLiteral("vulkan") ? ParakeetDevice::Vulkan : ParakeetDevice::Cpu;
    const QString modelPath = args.at(2);
    const QString wavPath = args.at(3);
    const int chunkMs = args.size() > 4 ? args.at(4).toInt() : 500;

    // 1. Load + resample the WAV to 16 kHz mono float.
    WavData wav;
    if (!loadPcmWav(wavPath.toStdString(), wav)) {
        std::cerr << "[stream] failed to load WAV " << wavPath.toUtf8().constData() << "\n";
        return 1;
    }
    std::vector<float> pcm16k;
    if (wav.sampleRate == 16000) {
        pcm16k = wav.samples;
    } else {
        pcm16k = resampleLinearF32(wav.samples.data(), wav.samples.size(),
                                   wav.sampleRate, 16000);
    }
    std::cout << "[stream] " << wav.samples.size() << " samples @ " << wav.sampleRate
              << " Hz -> " << pcm16k.size() << " @ 16 kHz ("
              << (pcm16k.size() / 16000.0) << " s)\n";

    // 2. Load the backend + model.
    ParakeetBackend backend;
    if (!backend.load(device)) {
        std::cerr << "[stream] backend load failed: " << backend.lastError().toUtf8().constData() << "\n";
        return 1;
    }
    if (!backend.loadModel(modelPath)) {
        std::cerr << "[stream] model load failed: " << backend.lastError().toUtf8().constData() << "\n";
        return 1;
    }

    // 3. Open a streaming session and feed in chunks.
    if (!backend.streamBegin()) {
        std::cerr << "[stream] stream_begin failed: " << backend.lastError().toUtf8().constData() << "\n";
        return 1;
    }

    const int chunkSize = 16 * chunkMs; // e.g. 500 ms = 8000 samples
    const auto t0 = std::chrono::steady_clock::now();
    std::cout << "[stream] ";
    std::cout.flush();

    for (int off = 0; off < static_cast<int>(pcm16k.size()); off += chunkSize) {
        const int n = std::min(chunkSize, static_cast<int>(pcm16k.size()) - off);
        const StreamFeedResult r = backend.streamFeed(pcm16k.data() + off, n);
        if (!r.ok) {
            std::cerr << "\n[stream] feed failed: " << r.error.toUtf8().constData() << "\n";
            return 1;
        }
        if (!r.text.isEmpty()) {
            std::cout << r.text.toUtf8().constData();
            std::cout.flush();
        }
        if (r.eouMask & kParakeetEventEou) {
            std::cout << " [EOU]";
            std::cout.flush();
        }
        if (r.eouMask & kParakeetEventEob) {
            std::cout << " [EOB]";
            std::cout.flush();
        }
    }

    // 4. Flush the tail.
    const StreamFeedResult tail = backend.streamFinalize();
    if (!tail.ok) {
        std::cerr << "\n[stream] finalize failed: " << tail.error.toUtf8().constData() << "\n";
        return 1;
    }
    if (!tail.text.isEmpty()) {
        std::cout << tail.text.toUtf8().constData();
    }
    backend.streamEnd();

    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "\n[stream] done in " << ms << " ms\n";
    return 0;
}
