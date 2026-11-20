// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace ff
{

/** Number of perceptual analysis bands. They are spaced evenly on the ERB-rate
    scale, so every band covers roughly the same "amount of hearing". */
constexpr int kNumBands = 32;

constexpr float kLowestBandHz  = 30.0f;
constexpr float kHighestBandHz = 18000.0f;

/** ERB-rate (Glasberg & Moore): number of ERBs below frequency f. */
inline float hzToErbRate (float hz) noexcept   { return 21.4f * std::log10 (1.0f + 0.00437f * hz); }
inline float erbRateToHz (float erb) noexcept  { return (std::pow (10.0f, erb / 21.4f) - 1.0f) / 0.00437f; }

/** Equivalent rectangular bandwidth in Hz at frequency f. */
inline float erbWidthHz (float hz) noexcept    { return 24.7f * (4.37f * hz / 1000.0f + 1.0f); }

/** Centre frequencies and edges of the analysis bands. Immutable, cheap to copy. */
struct BandLayout
{
    std::array<float, kNumBands>     centreHz {};
    std::array<float, kNumBands + 1> edgeHz {};

    static const BandLayout& get() noexcept
    {
        static const BandLayout layout = make();
        return layout;
    }

    /** Index of the band whose range contains hz (clamped). */
    int bandForHz (float hz) const noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
            if (hz < edgeHz[(size_t) b + 1])
                return b;
        return kNumBands - 1;
    }

private:
    static BandLayout make() noexcept
    {
        BandLayout l;
        const float lo = hzToErbRate (kLowestBandHz);
        const float hi = hzToErbRate (kHighestBandHz);
        const float step = (hi - lo) / (float) (kNumBands - 1);

        for (int b = 0; b < kNumBands; ++b)
            l.centreHz[(size_t) b] = erbRateToHz (lo + step * (float) b);

        l.edgeHz[0] = erbRateToHz (lo - 0.5f * step);
        for (int b = 1; b < kNumBands; ++b)
            l.edgeHz[(size_t) b] = erbRateToHz (lo + step * ((float) b - 0.5f));
        l.edgeHz[kNumBands] = erbRateToHz (hi + 0.5f * step);
        return l;
    }
};

using BandArray = std::array<float, kNumBands>;

inline float dbToGain (float db) noexcept    { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) noexcept     { return 20.0f * std::log10 (g > 1.0e-9f ? g : 1.0e-9f); }
inline float powerToDb (float p) noexcept    { return 10.0f * std::log10 (p > 1.0e-12f ? p : 1.0e-12f); }
inline float dbToPower (float db) noexcept   { return std::pow (10.0f, db / 10.0f); }

constexpr float kSilenceDb = -120.0f;

} // namespace ff
