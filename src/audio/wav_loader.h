/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Minimal PCM WAV loader.
 *
 * Reads a standard RIFF/WAVE file (PCM format, 8/16/24/32-bit integer), downmixes
 * to mono, and returns float32 samples at the file's native sample rate. Kea then
 * resamples to 16 kHz before feeding parakeet. This is deliberately small — just
 * enough for tests and the streaming smoke path; the recorder (live audio) does
 * not use it.
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace kea {

struct WavData
{
    std::vector<float> samples; ///< mono float32, normalized to [-1, 1]
    int sampleRate = 0;
    int channels = 0; ///< original channel count (before downmix)
    int bitsPerSample = 0;
};

/// Load a PCM WAV file. Returns false on any parse error or non-PCM format.
bool loadPcmWav(const std::string &path, WavData &out);

} // namespace kea
