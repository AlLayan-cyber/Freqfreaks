// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/BellBank.h"
#include "ff/Biquad.h"

#include <algorithm>
#include <cmath>

namespace ff
{

void BellBank::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    // Even spacing in octaves from 40 Hz to 14 kHz.
    constexpr float lowHz = 40.0f, highHz = 14000.0f;
    const float octaves = std::log2 (highHz / lowHz);
    const float stepOct = octaves / (float) (kNumBells - 1);

    // Bandwidth of 1.4 bell spacings, so neighbours overlap smoothly: Q from bandwidth in octaves.
    const float bw = 1.4f * stepOct;
    const float p2 = std::pow (2.0f, bw);
    const float q = std::sqrt (p2) / (p2 - 1.0f);

    for (int k = 0; k < kNumBells; ++k)
    {
        fc[(size_t) k] = lowHz * std::pow (2.0f, stepOct * (float) k);
        qs[(size_t) k] = q;
    }

    const auto& layout = BandLayout::get();
    constexpr double probeDb = -6.0;
    for (int k = 0; k < kNumBells; ++k)
    {
        const auto c = BiquadCoeffs::peaking (rate, fc[(size_t) k], qs[(size_t) k], probeDb);
        for (int b = 0; b < kNumBands; ++b)
        {
            const double hz = std::min ((double) layout.centreHz[(size_t) b], rate * 0.49);
            shape[(size_t) b][(size_t) k] = (float) (c.magnitudeDb (hz, rate) / probeDb);
        }
    }

}

float BellBank::largestEigen (const BandArray& weight) const noexcept
{
    // Power iteration on A^T W A.
    std::array<double, kNumBells> v {};
    v.fill (1.0);
    double lambda = 1.0;
    for (int it = 0; it < 24; ++it)
    {
        std::array<double, kNumBands> av {};
        for (int b = 0; b < kNumBands; ++b)
        {
            for (int k = 0; k < kNumBells; ++k)
                av[(size_t) b] += shape[(size_t) b][(size_t) k] * v[(size_t) k];
            av[(size_t) b] *= weight[(size_t) b];
        }

        std::array<double, kNumBells> next {};
        for (int k = 0; k < kNumBells; ++k)
            for (int b = 0; b < kNumBands; ++b)
                next[(size_t) k] += shape[(size_t) b][(size_t) k] * av[(size_t) b];

        double norm = 0.0;
        for (auto x : next)
            norm += x * x;
        norm = std::sqrt (norm);
        if (norm <= 0.0)
            break;
        lambda = norm;
        for (int k = 0; k < kNumBells; ++k)
            v[(size_t) k] = next[(size_t) k] / norm;
    }
    return (float) std::max (lambda, 1.0e-6);
}

BandArray BellBank::rangeWeights (float lowHz, float highHz, float outsideWeight) noexcept
{
    const auto& layout = BandLayout::get();
    BandArray w {};
    for (int b = 0; b < kNumBands; ++b)
    {
        const float hz = layout.centreHz[(size_t) b];
        w[(size_t) b] = (hz >= lowHz && hz <= highHz) ? 1.0f : outsideWeight;
    }
    return w;
}

std::array<bool, kNumBells> BellBank::bellsWithin (float lowHz, float highHz) const noexcept
{
    std::array<bool, kNumBells> ok {};
    for (int k = 0; k < kNumBells; ++k)
        ok[(size_t) k] = fc[(size_t) k] >= lowHz && fc[(size_t) k] <= highHz;
    return ok;
}

void BellBank::fit (const BandArray& cutDb, const BandArray& weight, const std::array<bool, kNumBells>& allowed,
                    float maxCutDb, BellArray& gains) const noexcept
{
    for (int k = 0; k < kNumBells; ++k)
        if (! allowed[(size_t) k])
            gains[(size_t) k] = 0.0f;

    // Exact step for this weighting (the largest eigenvalue of A^T W A).
    const float step = 1.0f / largestEigen (weight);

    // Projected gradient descent on 0.5 * sum w (A g - cut)^2 with 0 <= g <= maxCut.
    for (int it = 0; it < 120; ++it)
    {
        std::array<float, kNumBands> residual {};
        for (int b = 0; b < kNumBands; ++b)
        {
            float r = -cutDb[(size_t) b];
            for (int k = 0; k < kNumBells; ++k)
                r += shape[(size_t) b][(size_t) k] * gains[(size_t) k];
            residual[(size_t) b] = weight[(size_t) b] * r;
        }

        for (int k = 0; k < kNumBells; ++k)
        {
            if (! allowed[(size_t) k])
                continue;
            float grad = 0.0f;
            for (int b = 0; b < kNumBands; ++b)
                grad += shape[(size_t) b][(size_t) k] * residual[(size_t) b];
            gains[(size_t) k] = std::clamp (gains[(size_t) k] - step * grad, 0.0f, maxCutDb);
        }
    }
}

float BellBank::cutAt (float hz, const BellArray& gains) const noexcept
{
    double db = 0.0;
    const double f = std::min ((double) hz, rate * 0.49);
    for (int k = 0; k < kNumBells; ++k)
        if (gains[(size_t) k] > 1.0e-3f)
            db += BiquadCoeffs::peaking (rate, fc[(size_t) k], qs[(size_t) k], -gains[(size_t) k]).magnitudeDb (f, rate);
    return (float) -db;
}

BandArray BellBank::response (const BellArray& gains) const noexcept
{
    BandArray out {};
    for (int b = 0; b < kNumBands; ++b)
    {
        float sum = 0.0f;
        for (int k = 0; k < kNumBells; ++k)
            sum += shape[(size_t) b][(size_t) k] * gains[(size_t) k];
        out[(size_t) b] = sum;
    }
    return out;
}

} // namespace ff
