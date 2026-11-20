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

    const float lo = hzToErbRate (45.0f);
    const float hi = hzToErbRate (14000.0f);
    const float step = (hi - lo) / (float) (kNumBells - 1);

    for (int k = 0; k < kNumBells; ++k)
    {
        const float hz = erbRateToHz (lo + step * (float) k);
        // Bandwidth spans about 1.4 bell spacings, so neighbours overlap smoothly.
        const float bwHz = 1.4f * step * erbWidthHz (hz);
        fc[(size_t) k] = hz;
        qs[(size_t) k] = std::clamp (hz / bwHz, 0.5f, 6.0f);
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

    // Largest eigenvalue of A^T A by power iteration -> safe gradient step.
    std::array<double, kNumBells> v {};
    v.fill (1.0);
    double lambda = 1.0;
    for (int it = 0; it < 50; ++it)
    {
        std::array<double, kNumBands> av {};
        for (int b = 0; b < kNumBands; ++b)
            for (int k = 0; k < kNumBells; ++k)
                av[(size_t) b] += shape[(size_t) b][(size_t) k] * v[(size_t) k];

        std::array<double, kNumBells> atav {};
        for (int k = 0; k < kNumBells; ++k)
            for (int b = 0; b < kNumBands; ++b)
                atav[(size_t) k] += shape[(size_t) b][(size_t) k] * av[(size_t) b];

        double norm = 0.0;
        for (auto x : atav)
            norm += x * x;
        norm = std::sqrt (norm);
        if (norm <= 0.0)
            break;
        lambda = norm;
        for (int k = 0; k < kNumBells; ++k)
            v[(size_t) k] = atav[(size_t) k] / norm;
    }
    stepSize = (float) (1.0 / std::max (lambda, 1.0e-6));
}

void BellBank::fit (const BandArray& cutDb, const std::array<bool, kNumBells>& allowed,
                    float maxCutDb, BellArray& gains) const noexcept
{
    for (int k = 0; k < kNumBells; ++k)
        if (! allowed[(size_t) k])
            gains[(size_t) k] = 0.0f;

    // Projected gradient descent on 0.5 * ||A g - cut||^2 with 0 <= g <= maxCut.
    for (int it = 0; it < 60; ++it)
    {
        std::array<float, kNumBands> residual {};
        for (int b = 0; b < kNumBands; ++b)
        {
            float r = -cutDb[(size_t) b];
            for (int k = 0; k < kNumBells; ++k)
                r += shape[(size_t) b][(size_t) k] * gains[(size_t) k];
            residual[(size_t) b] = r;
        }

        for (int k = 0; k < kNumBells; ++k)
        {
            if (! allowed[(size_t) k])
                continue;
            float grad = 0.0f;
            for (int b = 0; b < kNumBands; ++b)
                grad += shape[(size_t) b][(size_t) k] * residual[(size_t) b];
            gains[(size_t) k] = std::clamp (gains[(size_t) k] - stepSize * grad, 0.0f, maxCutDb);
        }
    }
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
