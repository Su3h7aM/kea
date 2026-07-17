/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-resampler-test — headless checks for the audio DSP layer.
 *
 *   1. int16 -> float scaling (-32768 -> -1.0, +32767 -> ~0.99997, 0 -> 0).
 *   2. Batch downsample length (48k -> 16k yields input/3 samples).
 *   3. Round-trip exactness: a 16k signal zero-order-upsampled by 3 to 48k,
 *      resampled back to 16k, recovers the original exactly (ratio is integer,
 *      interpolation lands on whole samples).
 *   4. Streaming resampler matches the batch result across chunk boundaries
 *      (the seamlessness invariant that matters for live audio).
 *
 * Returns 0 on success, 1 on any failure.
 */
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "audio/resampler.h"

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

static bool approx(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    std::printf("[resampler] int16 -> float scaling\n");
    {
        int16_t in[] = {-32768, 32767, 0, 16384};
        auto f = convertInt16ToFloat(in, 4);
        check(approx(f[0], -1.0f), "-32768 -> -1.0");
        check(f[1] < 1.0f && f[1] > 0.9999f, "+32767 -> ~0.99997 (< 1.0)");
        check(approx(f[2], 0.0f), "0 -> 0.0");
        check(approx(f[3], 0.5f), "+16384 -> 0.5");
    }

    std::printf("[resampler] batch downsample length (48k -> 16k)\n");
    {
        const std::size_t N = 48000;
        std::vector<int16_t> in(N, 0);
        auto out = resampleTo16kMonoF32(in.data(), in.size(), 48000);
        check(out.size() == 16000, "48000 -> 16000 samples (floor((48000-1)/3)+1)");
    }

    std::printf("[resampler] exact round-trip (16k signal upsampled x3, back to 16k)\n");
    {
        // Original 16k signal: a low-frequency sine (220 Hz), 1 second.
        const std::size_t N = 16000;
        std::vector<float> orig(N);
        for (std::size_t i = 0; i < N; ++i) {
            orig[i] = 0.8f * std::sin(2.0 * M_PI * 220.0 * i / 16000.0);
        }
        // Zero-order hold upsample x3 to 48k (each sample repeated 3x). This is
        // band-limited to <8 kHz, so downsampling back must be lossless and land
        // on whole samples (src index = i*3 exactly).
        std::vector<float> up(N * 3);
        for (std::size_t i = 0; i < N; ++i) {
            up[i * 3] = orig[i];
            up[i * 3 + 1] = orig[i];
            up[i * 3 + 2] = orig[i];
        }
        auto back = resampleLinearF32(up.data(), up.size(), 48000, 16000);
        // Compare the first N samples (output may have one extra tail sample).
        bool match = true;
        const std::size_t cmp = std::min(back.size(), N);
        for (std::size_t i = 0; i < cmp; ++i) {
            if (!approx(back[i], orig[i], 1e-4f)) {
                std::printf("    mismatch at %zu: got %.6f want %.6f\n", i, back[i], orig[i]);
                match = false;
                break;
            }
        }
        check(match && cmp == N, "round-trip recovers original 16k signal");
    }

    std::printf("[resampler] streaming matches batch across chunk boundaries\n");
    {
        // Build a 48k test signal (sweep), resample in one shot vs. in 17-sample
        // chunks via the streaming resampler; results must match.
        const std::size_t N = 48017; // intentionally not chunk-aligned
        std::vector<int16_t> in(N);
        for (std::size_t i = 0; i < N; ++i) {
            in[i] = static_cast<int16_t>(16000.0 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
        }
        const auto batch = resampleTo16kMonoF32(in.data(), in.size(), 48000);

        StreamingResampler sr(48000, 16000);
        std::vector<float> streamed;
        const std::size_t chunk = 17;
        for (std::size_t i = 0; i < N; i += chunk) {
            const std::size_t take = std::min(chunk, N - i);
            auto blk = sr.push(in.data() + i, take);
            streamed.insert(streamed.end(), blk.begin(), blk.end());
        }
        // Note: batch and streaming may differ by at most the final held sample
        // (flush). Compare up to min length with a small tolerance.
        const std::size_t cmp = std::min(batch.size(), streamed.size());
        bool match = true;
        double maxErr = 0.0;
        for (std::size_t i = 0; i < cmp; ++i) {
            const double e = std::fabs(batch[i] - streamed[i]);
            maxErr = std::max(maxErr, e);
            if (e > 1e-5) {
                std::printf("    mismatch at %zu: batch=%.6f stream=%.6f\n", i, batch[i], streamed[i]);
                match = false;
                break;
            }
        }
        std::printf("    compared %zu samples, max abs err = %.2e\n", cmp, maxErr);
        check(match, "streaming output matches batch (seamless across chunks)");
    }

    if (failures == 0) {
        std::printf("[resampler] ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("[resampler] %d TEST(S) FAILED\n", failures);
    return 1;
}
