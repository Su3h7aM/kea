/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "wav_loader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace kea {

namespace {

// Read a little-endian unsigned integer of N bytes from a byte buffer.
template <std::size_t N>
uint64_t readLE(const unsigned char *p)
{
    uint64_t v = 0;
    for (std::size_t i = 0; i < N; ++i) {
        v |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

// Convert one raw PCM sample to float, centered and normalized.
float sampleToFloat(const unsigned char *p, int bitsPerSample)
{
    switch (bitsPerSample) {
    case 8: {
        // 8-bit WAV is unsigned, 0..255, centered at 128.
        const int v = static_cast<int>(p[0]) - 128;
        return static_cast<float>(v) / 128.0f;
    }
    case 16: {
        const int16_t v = static_cast<int16_t>(readLE<2>(p));
        return static_cast<float>(v) / 32768.0f;
    }
    case 24: {
        int32_t v = static_cast<int8_t>(p[2]); // sign-extend the high byte
        v = (v << 16) | (p[1] << 8) | p[0];
        return static_cast<float>(v) / 8388608.0f;
    }
    case 32: {
        const int32_t v = static_cast<int32_t>(readLE<4>(p));
        return static_cast<float>(v) / 2147483648.0f;
    }
    default:
        return 0.0f;
    }
}

} // namespace

bool loadPcmWav(const std::string &path, WavData &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    if (buf.size() < 44) {
        return false; // smaller than a minimal WAV header
    }

    // RIFF header.
    if (std::memcmp(buf.data() + 0, "RIFF", 4) != 0 ||
        std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    int audioFormat = -1;
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    const unsigned char *data = nullptr;
    std::size_t dataLen = 0;

    // Walk the chunks starting at offset 12.
    std::size_t off = 12;
    while (off + 8 <= buf.size()) {
        const unsigned char *chunk = buf.data() + off;
        const std::size_t chunkSize = static_cast<std::size_t>(readLE<4>(chunk + 4));
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (chunkSize < 16 || off + 8 + 16 > buf.size()) {
                return false;
            }
            audioFormat = static_cast<int>(readLE<2>(chunk + 8));
            channels = static_cast<int>(readLE<2>(chunk + 10));
            sampleRate = static_cast<int>(readLE<4>(chunk + 12));
            bitsPerSample = static_cast<int>(readLE<2>(chunk + 22));
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            dataLen = std::min(chunkSize, buf.size() - (off + 8));
        }
        // Chunks are word-aligned: advance by size padded to even.
        off += 8 + chunkSize + (chunkSize & 1);
    }

    if (audioFormat != 1 || channels <= 0 || sampleRate <= 0 || bitsPerSample <= 0) {
        return false; // only uncompressed PCM is supported
    }
    if (!data || dataLen == 0) {
        return false;
    }

    const int bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample <= 0) {
        return false;
    }
    const int frameSize = bytesPerSample * channels;
    const std::size_t nFrames = dataLen / static_cast<std::size_t>(frameSize);

    out.samples.clear();
    out.samples.reserve(nFrames);
    for (std::size_t i = 0; i < nFrames; ++i) {
        const unsigned char *frame = data + i * static_cast<std::size_t>(frameSize);
        // Downmix to mono by averaging all channels.
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c) {
            mono += sampleToFloat(frame + c * bytesPerSample, bitsPerSample);
        }
        out.samples.push_back(mono / static_cast<float>(channels));
    }

    out.sampleRate = sampleRate;
    out.channels = channels;
    out.bitsPerSample = bitsPerSample;
    return true;
}

} // namespace kea
