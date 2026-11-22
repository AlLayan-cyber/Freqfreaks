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

/** Fine analysis for the narrow Sharp mode: 96 bands of ~1/11 octave from 40 Hz to 16 kHz,
    so a narrow conflict shows up where it really is. (Below ~300 Hz the FFT's own
    resolution is coarser than this; neighbouring fine bands then share bins.) */
constexpr int kFineBands = 96;
using FineArray = std::array<float, kFineBands>;

struct FineLayout
{
    std::array<float, kFineBands>     centreHz {};
    std::array<float, kFineBands + 1> edgeHz {};
    std::array<int, kFineBands>       coarseBand {};   // the analysis band each fine band lies in
    std::array<float, kFineBands>     erbRate {};

    static const FineLayout& get() noexcept
    {
        static const FineLayout layout = make();
        return layout;
    }

private:
    static FineLayout make() noexcept
    {
        FineLayout l;
        const float lo = std::log2 (40.0f), hi = std::log2 (16000.0f);
        const float step = (hi - lo) / (float) (kFineBands - 1);
        for (int b = 0; b < kFineBands; ++b)
        {
            l.centreHz[(size_t) b] = std::pow (2.0f, lo + step * (float) b);
            l.coarseBand[(size_t) b] = BandLayout::get().bandForHz (l.centreHz[(size_t) b]);
            l.erbRate[(size_t) b] = hzToErbRate (l.centreHz[(size_t) b]);
        }
        for (int b = 0; b <= kFineBands; ++b)
            l.edgeHz[(size_t) b] = std::pow (2.0f, lo + step * ((float) b - 0.5f));
        return l;
    }
};

inline float dbToGain (float db) noexcept    { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) noexcept     { return 20.0f * std::log10 (g > 1.0e-9f ? g : 1.0e-9f); }
inline float powerToDb (float p) noexcept    { return 10.0f * std::log10 (p > 1.0e-12f ? p : 1.0e-12f); }
inline float dbToPower (float db) noexcept   { return std::pow (10.0f, db / 10.0f); }

constexpr float kSilenceDb = -120.0f;

} // namespace ff
