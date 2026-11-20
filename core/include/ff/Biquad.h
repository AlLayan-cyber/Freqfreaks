// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace ff
{

/** Normalised biquad coefficients (a0 = 1), RBJ cookbook designs. */
struct BiquadCoeffs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    static BiquadCoeffs peaking (double sampleRate, double hz, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 6.283185307179586 * clampHz (hz, sampleRate) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double c = std::cos (w0);
        const double a0 = 1.0 + alpha / A;
        return make ((1.0 + alpha * A) / a0, (-2.0 * c) / a0, (1.0 - alpha * A) / a0,
                     (-2.0 * c) / a0, (1.0 - alpha / A) / a0);
    }

    /** Band-pass with 0 dB gain at the centre frequency. */
    static BiquadCoeffs bandpass (double sampleRate, double hz, double q) noexcept
    {
        const double w0 = 6.283185307179586 * clampHz (hz, sampleRate) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double c = std::cos (w0);
        const double a0 = 1.0 + alpha;
        return make (alpha / a0, 0.0, -alpha / a0, (-2.0 * c) / a0, (1.0 - alpha) / a0);
    }

    double magnitude (double hz, double sampleRate) const noexcept
    {
        const auto z = std::polar (1.0, -6.283185307179586 * hz / sampleRate);   // z^-1
        const auto num = (double) b0 + (double) b1 * z + (double) b2 * z * z;
        const auto den = 1.0 + (double) a1 * z + (double) a2 * z * z;
        return std::abs (num / den);
    }

    double magnitudeDb (double hz, double sampleRate) const noexcept
    {
        return 20.0 * std::log10 (std::max (magnitude (hz, sampleRate), 1.0e-9));
    }

private:
    static double clampHz (double hz, double sampleRate) noexcept
    {
        return std::min (std::max (hz, 10.0), sampleRate * 0.49);
    }

    static BiquadCoeffs make (double b0, double b1, double b2, double a1, double a2) noexcept
    {
        return { (float) b0, (float) b1, (float) b2, (float) a1, (float) a2 };
    }
};

/** Transposed direct form II state for one channel. */
struct BiquadState
{
    float z1 = 0.0f, z2 = 0.0f;

    inline float process (float x, const BiquadCoeffs& c) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }
};

} // namespace ff
