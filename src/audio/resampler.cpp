/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "resampler.h"

#include <algorithm>
#include <cmath>

namespace kea {

std::vector<float> convertInt16ToFloat(const int16_t *in, std::size_t n)
{
    std::vector<float> out;
    out.reserve(n);
    const float scale = 1.0f / 32768.0f;
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<float>(in[i]) * scale);
    }
    return out;
}

std::vector<float> resampleLinearF32(const float *in, std::size_t n,
                                     int inRate, int outRate)
{
    std::vector<float> out;
    if (n == 0 || inRate <= 0 || outRate <= 0) {
        return out;
    }
    if (inRate == outRate) {
        out.assign(in, in + n);
        return out;
    }

    const double step = static_cast<double>(inRate) / static_cast<double>(outRate);
    // Number of output samples: how many whole steps fit before the last input sample.
    std::size_t nOut = 0;
    if (n >= 2) {
        nOut = static_cast<std::size_t>(std::floor((n - 1) / step)) + 1;
    } else {
        // Single sample: emit it once.
        out.push_back(in[0]);
        return out;
    }
    out.reserve(nOut);

    double pos = 0.0;
    for (std::size_t i = 0; i < nOut; ++i) {
        const auto j = static_cast<std::size_t>(pos);
        const double frac = pos - static_cast<double>(j);
        // j is always < n here because nOut is bounded by (n-1)/step, but j+1
        // can land exactly on n when pos rounds to the last input sample
        // (e.g. step is a whole number and n-1 is a multiple of it) — guard
        // against reading one element past the end.
        const float a = in[j];
        const float b = (j + 1 < n) ? in[j + 1] : a;
        out.push_back(static_cast<float>(a + (b - a) * frac));
        pos += step;
    }
    return out;
}

std::vector<float> resampleTo16kMonoF32(const int16_t *in, std::size_t n, int inRate)
{
    const std::vector<float> f = convertInt16ToFloat(in, n);
    return resampleLinearF32(f.data(), f.size(), inRate, 16000);
}

// --- StreamingResampler ---------------------------------------------------

StreamingResampler::StreamingResampler(int inRate, int outRate)
    : m_step(static_cast<double>(inRate) / static_cast<double>(outRate))
{
}

std::vector<float> StreamingResampler::push(const int16_t *in, std::size_t n)
{
    // Append converted input to the pending buffer.
    const auto conv = convertInt16ToFloat(in, n);
    m_in.insert(m_in.end(), conv.begin(), conv.end());

    std::vector<float> out;
    // Emit while we have two samples to interpolate between.
    while (m_in.size() >= 2 && m_pos + 1.0 <= static_cast<double>(m_in.size() - 1)) {
        const auto j = static_cast<std::size_t>(m_pos);
        const double frac = m_pos - static_cast<double>(j);
        const float a = m_in[j];
        const float b = m_in[j + 1];
        out.push_back(static_cast<float>(a + (b - a) * frac));
        m_pos += m_step;
    }

    // Drop consumed input, keeping one sample before the fractional position so
    // the next call can interpolate across the boundary.
    const auto drop = static_cast<std::size_t>(std::floor(m_pos));
    if (drop > 0) {
        const std::size_t maxDrop = m_in.size() > 1 ? m_in.size() - 1 : 0;
        const std::size_t d = std::min(drop, maxDrop);
        m_in.erase(m_in.begin(), m_in.begin() + static_cast<std::ptrdiff_t>(d));
        m_pos -= static_cast<double>(d);
    }
    return out;
}

std::vector<float> StreamingResampler::flush()
{
    std::vector<float> out;
    // Emit the final held sample (the one at the fractional tail) as the EOS.
    if (!m_in.empty()) {
        const auto j = static_cast<std::size_t>(std::floor(m_pos));
        if (j < m_in.size()) {
            out.push_back(m_in[j]);
        } else if (!m_in.empty()) {
            out.push_back(m_in.back());
        }
    }
    reset();
    return out;
}

void StreamingResampler::reset()
{
    m_pos = 0.0;
    m_in.clear();
}

} // namespace kea
