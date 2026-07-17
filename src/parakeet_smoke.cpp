/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-parakeet-smoke — Phase 0 end-to-end check of the parakeet.cpp loader.
 *
 * Loads the requested backend, loads a model GGUF, transcribes a WAV, and
 * prints the transcript. Usage:
 *
 *   kea-parakeet-smoke <backend:cpu|vulkan> <model.gguf> <audio.wav>
 */
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <iostream>

#include "inference/parakeet_backend.h"

using namespace kea;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 4) {
        std::cerr << "usage: kea-parakeet-smoke <cpu|vulkan> <model.gguf> <audio.wav>\n";
        return 2;
    }

    const ParakeetDevice device =
        args.at(1) == QStringLiteral("vulkan") ? ParakeetDevice::Vulkan : ParakeetDevice::Cpu;

    ParakeetBackend backend;
    std::cout << "[smoke] loading backend (" << (args.at(1).toUtf8().constData()) << ")...\n";
    if (!backend.load(device)) {
        std::cerr << "[smoke] load failed: " << backend.lastError().toUtf8().constData() << "\n";
        return 1;
    }
    std::cout << "[smoke] backend loaded. loading model " << args.at(2).toUtf8().constData() << "...\n";
    if (!backend.loadModel(args.at(2))) {
        std::cerr << "[smoke] model load failed: " << backend.lastError().toUtf8().constData() << "\n";
        return 1;
    }
    std::cout << "[smoke] transcribing " << args.at(3).toUtf8().constData() << "...\n";
    const TranscriptionResult r = backend.transcribePath(args.at(3));
    if (!r.ok) {
        std::cerr << "[smoke] transcribe failed: " << r.text.toUtf8().constData() << "\n";
        return 1;
    }
    std::cout << "[smoke] transcript:\n" << r.text.toUtf8().constData() << "\n";
    return 0;
}
