/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Audio resampling and format conversion.
 *
 * parakeet.cpp's streaming path consumes 16 kHz mono float32 PCM. PipeWire (via
 * Qt Multimedia) delivers 48 kHz int16 audio, so Kea converts and downsamples
 * before feeding the model. We use linear interpolation — matching parakeet's
 * own internal linear resampler (src/audio_io.cpp) for consistency.
 */
#pragma once

#include <cstdint>
#include <vector>

namespace kea {

/// Convert int16 PCM samples to float32 normalized to [-1, 1].
/// (-32768 -> -1.0, 32767 -> ~0.99997, 0 -> 0).
std::vector<float> convertInt16ToFloat(const int16_t *in, std::size_t n);

/// Batch linear resample of float PCM from `inRate` to `outRate`.
std::vector<float> resampleLinearF32(const float *in, std::size_t n,
                                     int inRate, int outRate);

/// Convenience: int16 @ inRate -> float @ 16000 Hz (what parakeet expects).
std::vector<float> resampleTo16kMonoF32(const int16_t *in, std::size_t n, int inRate);

/// Stateful streaming resampler for continuous live audio.
///
/// Live audio arrives in arbitrary-sized int16 chunks from QAudioSource. A naive
/// per-chunk resample would introduce a discontinuity at every chunk boundary
/// because the fractional output phase resets. This class carries the fractional
/// input position and a trailing sample across calls so the output stream is
/// seamless, no matter how the input is chunked.
class StreamingResampler
{
public:
    /// `inRate` is the device capture rate; `outRate` defaults to 16000.
    explicit StreamingResampler(int inRate, int outRate = 16000);

    /// Push an int16 chunk; returns the float@outRate samples produced this call
    /// (may be empty if not enough input has accumulated for a new output sample).
    std::vector<float> push(const int16_t *in, std::size_t n);

    /// Flush the end of stream: emit any final interpolated tail sample.
    std::vector<float> flush();

    void reset();

private:
    const double m_step;     ///< input samples consumed per output sample (inRate/outRate)
    double m_pos = 0.0;      ///< fractional input index of the next output
    std::vector<float> m_in; ///< pending input samples not yet consumed
};

} // namespace kea
