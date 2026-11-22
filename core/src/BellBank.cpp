// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/BellBank.h"
#include "ff/Biquad.h"

#include <algorithm>
#include <cmath>

namespace ff
{

void BellBank::prepare (double sampleRate, int sharpStep)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    stepIndex = sharpStep < 0 ? 0 : (sharpStep >= kSharpSteps ? kSharpSteps - 1 : sharpStep);
    count = bellsForStep (stepIndex);
    oversample = count > 16 ? 2 : 1;
    grid = count > 16 ? kMaxGrid : 32;

    // Even spacing in octaves from 40 Hz to 14 kHz.
    constexpr float lowHz = 40.0f, highHz = 14000.0f;
    const float octaves = std::log2 (highHz / lowHz);
    const float stepOct = octaves / (float) (count - 1);

    // Bandwidth of 1.4 bell spacings, so neighbours overlap smoothly: Q from bandwidth in octaves.
    const float bw = 1.4f * stepOct;
    const float p2 = std::pow (2.0f, bw);
    const float q = std::sqrt (p2) / (p2 - 1.0f);

    fc.fill (0.0f);
    qs.fill (0.0f);
    for (int k = 0; k < count; ++k)
    {
        fc[(size_t) k] = lowHz * std::pow (2.0f, stepOct * (float) k);
        qs[(size_t) k] = q;
    }

    // Fine grid for the transfer fit: the Space range is within 60 Hz .. 16 kHz.
    for (auto& row : gridShape)
        row.fill (0.0f);
    for (auto& row : gridBandpass)
        row.fill ({});
    for (int j = 0; j < grid; ++j)
    {
        const double hz = std::min (60.0 * std::pow (16000.0 / 60.0, (double) j / (grid - 1)), rate * 0.49);
        const auto z = std::polar (1.0, -6.283185307179586 * hz / rate);
        for (int k = 0; k < count; ++k)
        {
            const auto peak = BiquadCoeffs::peaking (rate, fc[(size_t) k], qs[(size_t) k], -6.0);
            gridShape[(size_t) j][(size_t) k] = (float) (peak.magnitudeDb (hz, rate) / -6.0);

            const auto bp = BiquadCoeffs::bandpass (rate, fc[(size_t) k], qs[(size_t) k]);
            const auto num = (double) bp.b0 + (double) bp.b1 * z + (double) bp.b2 * z * z;
            const auto den = 1.0 + (double) bp.a1 * z + (double) bp.a2 * z * z;
            gridBandpass[(size_t) j][(size_t) k] = std::complex<float> (num / den);
        }
    }

    // Step for the transfer fit from the band-pass magnitudes (Gauss-Newton style bound).
    {
        std::array<double, kNumBells> v {};
        v.fill (1.0);
        double lambda = 1.0;
        for (int it = 0; it < 30; ++it)
        {
            std::array<double, kMaxGrid> av {};
            for (int j = 0; j < grid; ++j)
                for (int k = 0; k < count; ++k)
                    av[(size_t) j] += std::abs (gridBandpass[(size_t) j][(size_t) k]) * v[(size_t) k];
            std::array<double, kNumBells> next {};
            for (int k = 0; k < count; ++k)
                for (int j = 0; j < grid; ++j)
                    next[(size_t) k] += std::abs (gridBandpass[(size_t) j][(size_t) k]) * av[(size_t) j];
            double norm = 0.0;
            for (auto x : next)
                norm += x * x;
            norm = std::sqrt (norm);
            if (norm <= 0.0)
                break;
            lambda = norm;
            for (int k = 0; k < count; ++k)
                v[(size_t) k] = next[(size_t) k] / norm;
        }
        transferStep = (float) (1.0 / std::max (lambda, 1.0e-6));
    }

    // Fit points: band centres, and for the narrow banks also the log-midpoints between them.
    const auto& layout = BandLayout::get();
    constexpr double probeDb = -6.0;
    for (auto& row : shape)
        row.fill (0.0f);
    for (int k = 0; k < count; ++k)
    {
        const auto c = BiquadCoeffs::peaking (rate, fc[(size_t) k], qs[(size_t) k], probeDb);
        for (int j = 0; j < fitPoints(); ++j)
        {
            const int b = j / oversample;
            double hz = layout.centreHz[(size_t) b];
            if (j % oversample != 0)
                hz = std::sqrt (hz * (double) layout.centreHz[(size_t) b + 1]);
            hz = std::min (hz, rate * 0.49);
            shape[(size_t) j][(size_t) k] = (float) (c.magnitudeDb (hz, rate) / probeDb);
        }
    }

    for (auto& e : eigenCache)
        e = {};
}

void BellBank::expand (const BandArray& perBand, float* out, bool takeMax) const noexcept
{
    if (oversample == 1)
    {
        std::copy (perBand.begin(), perBand.end(), out);
        return;
    }
    for (int b = 0; b < kNumBands; ++b)
    {
        out[2 * b] = perBand[(size_t) b];
        if (b + 1 < kNumBands)
            out[2 * b + 1] = takeMax ? std::max (perBand[(size_t) b], perBand[(size_t) b + 1])
                                     : 0.5f * (perBand[(size_t) b] + perBand[(size_t) b + 1]);
    }
}

float BellBank::largestEigen (const BandArray& bandWeight) const noexcept
{
    // Power iteration on A^T W A.
    std::array<float, kMaxFitPoints> weight {};
    expand (bandWeight, weight.data(), true);
    const int points = fitPoints();

    std::array<double, kNumBells> v {};
    v.fill (1.0);
    double lambda = 1.0;
    for (int it = 0; it < 24; ++it)
    {
        std::array<double, kMaxFitPoints> av {};
        for (int j = 0; j < points; ++j)
        {
            for (int k = 0; k < count; ++k)
                av[(size_t) j] += shape[(size_t) j][(size_t) k] * v[(size_t) k];
            av[(size_t) j] *= weight[(size_t) j];
        }

        std::array<double, kNumBells> next {};
        for (int k = 0; k < count; ++k)
            for (int j = 0; j < points; ++j)
                next[(size_t) k] += shape[(size_t) j][(size_t) k] * av[(size_t) j];

        double norm = 0.0;
        for (auto x : next)
            norm += x * x;
        norm = std::sqrt (norm);
        if (norm <= 0.0)
            break;
        lambda = norm;
        for (int k = 0; k < count; ++k)
            v[(size_t) k] = next[(size_t) k] / norm;
    }
    return (float) std::max (lambda, 1.0e-6);
}

float BellBank::cachedEigen (const BandArray& weight) const noexcept
{
    for (const auto& e : eigenCache)
        if (e.value > 0.0f && e.weight == weight)
            return e.value;
    auto& slot = eigenCache[(size_t) eigenNext];
    eigenNext = (eigenNext + 1) % (int) eigenCache.size();
    slot.weight = weight;
    slot.value = largestEigen (weight);
    return slot.value;
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
    for (int k = 0; k < count; ++k)
        ok[(size_t) k] = fc[(size_t) k] >= lowHz && fc[(size_t) k] <= highHz;
    return ok;
}

void BellBank::fit (const BandArray& cutDb, const BandArray& bandWeight, const std::array<bool, kNumBells>& allowed,
                    float maxCutDb, BellArray& gains) const noexcept
{
    for (int k = 0; k < kNumBells; ++k)
        if (k >= count || ! allowed[(size_t) k])
            gains[(size_t) k] = 0.0f;

    // Exact step for this weighting (the largest eigenvalue of A^T W A).
    const float step = 1.0f / cachedEigen (bandWeight);

    std::array<float, kMaxFitPoints> target {}, weight {};
    expand (cutDb, target.data(), false);
    expand (bandWeight, weight.data(), true);
    const int points = fitPoints();

    // Projected gradient descent on 0.5 * sum w (A g - cut)^2 with 0 <= g <= maxCut.
    // Warm-started every analysis frame (~90 per second), so a few dozen steps per frame
    // converge within tens of milliseconds while keeping the audio thread's work small.
    for (int it = 0; it < 32; ++it)
    {
        std::array<float, kMaxFitPoints> residual {};
        for (int j = 0; j < points; ++j)
        {
            float r = -target[(size_t) j];
            for (int k = 0; k < count; ++k)
                r += shape[(size_t) j][(size_t) k] * gains[(size_t) k];
            residual[(size_t) j] = weight[(size_t) j] * r;
        }

        for (int k = 0; k < count; ++k)
        {
            if (! allowed[(size_t) k])
                continue;
            float grad = 0.0f;
            for (int j = 0; j < points; ++j)
                grad += shape[(size_t) j][(size_t) k] * residual[(size_t) j];
            gains[(size_t) k] = std::clamp (gains[(size_t) k] - step * grad, 0.0f, maxCutDb);
        }
    }
}

float BellBank::cutAt (float hz, const BellArray& gains) const noexcept
{
    double db = 0.0;
    const double f = std::min ((double) hz, rate * 0.49);
    for (int k = 0; k < count; ++k)
        if (gains[(size_t) k] > 1.0e-3f)
            db += BiquadCoeffs::peaking (rate, fc[(size_t) k], qs[(size_t) k], -gains[(size_t) k]).magnitudeDb (f, rate);
    return (float) -db;
}

void BellBank::fitTransfer (const BellArray& spaceCutDb, BellArray& weights, int iterations) const noexcept
{
    // Target magnitude of the transfer at each grid point: sqrt(1 - |H|^2).
    std::array<float, kMaxGrid> target {};
    bool any = false;
    for (int j = 0; j < grid; ++j)
    {
        float cut = 0.0f;
        for (int k = 0; k < count; ++k)
            cut += gridShape[(size_t) j][(size_t) k] * spaceCutDb[(size_t) k];
        const float h2 = std::pow (10.0f, -std::max (0.0f, cut) / 10.0f);
        target[(size_t) j] = std::sqrt (std::max (0.0f, 1.0f - h2));
        any = any || target[(size_t) j] > 1.0e-4f;
    }

    if (! any)
    {
        weights.fill (0.0f);
        return;
    }
    for (int k = count; k < kNumBells; ++k)
        weights[(size_t) k] = 0.0f;

    // Start where nothing is known yet: each bell carries its own share.
    for (int k = 0; k < count; ++k)
        if (weights[(size_t) k] <= 0.0f && spaceCutDb[(size_t) k] > 1.0e-3f)
        {
            const float c = std::pow (10.0f, -spaceCutDb[(size_t) k] / 20.0f);
            weights[(size_t) k] = 0.6f * std::sqrt (std::max (0.0f, 1.0f - c * c));
        }

    // Projected gradient on 0.5 * sum_j (|T_j| - target_j)^2, w >= 0 (warm-started per frame).
    for (int it = 0; it < iterations; ++it)
    {
        std::array<float, kNumBells> grad {};
        for (int j = 0; j < grid; ++j)
        {
            std::complex<float> t {};
            for (int k = 0; k < count; ++k)
                t += weights[(size_t) k] * gridBandpass[(size_t) j][(size_t) k];
            const float mag = std::abs (t);
            if (mag < 1.0e-6f)
            {
                // At zero the magnitude is not differentiable: push along |BP| towards the target.
                for (int k = 0; k < count; ++k)
                    grad[(size_t) k] -= target[(size_t) j] * std::abs (gridBandpass[(size_t) j][(size_t) k]);
                continue;
            }
            const float e = mag - target[(size_t) j];
            for (int k = 0; k < count; ++k)
                grad[(size_t) k] += e * (std::conj (t) * gridBandpass[(size_t) j][(size_t) k]).real() / mag;
        }
        for (int k = 0; k < count; ++k)
            weights[(size_t) k] = std::clamp (weights[(size_t) k] - transferStep * grad[(size_t) k], 0.0f, 1.5f);
    }
}

float BellBank::transferErrorDb (const BellArray& spaceCutDb, const BellArray& weights, float minMovedDb) const noexcept
{
    float worst = 0.0f;
    for (int j = 0; j < grid; ++j)
    {
        float cut = 0.0f;
        for (int k = 0; k < count; ++k)
            cut += gridShape[(size_t) j][(size_t) k] * spaceCutDb[(size_t) k];
        if (cut < minMovedDb)
            continue;
        const float wanted = 1.0f - std::pow (10.0f, -cut / 10.0f);
        std::complex<float> t {};
        for (int k = 0; k < count; ++k)
            t += weights[(size_t) k] * gridBandpass[(size_t) j][(size_t) k];
        const float got = std::norm (t);
        // Error in total band energy (Mid kept + Side added) relative to 1.
        const float totalDb = 10.0f * std::log10 (std::max (1.0e-9f, 1.0f - wanted + got));
        worst = std::max (worst, std::abs (totalDb));
    }
    return worst;
}

void BellBank::cutCurve (const BellArray& gains, const float* hz, float* cutDb, int numPoints) const noexcept
{
    std::array<BiquadCoeffs, kNumBells> coeffs {};
    std::array<bool, kNumBells> active {};
    for (int k = 0; k < kNumBells; ++k)
    {
        active[(size_t) k] = k < count && gains[(size_t) k] > 1.0e-3f;
        if (active[(size_t) k])
            coeffs[(size_t) k] = BiquadCoeffs::peaking (rate, fc[(size_t) k], qs[(size_t) k], -gains[(size_t) k]);
    }

    for (int i = 0; i < numPoints; ++i)
    {
        const double f = std::min ((double) hz[i], rate * 0.49);
        double db = 0.0;
        for (int k = 0; k < kNumBells; ++k)
            if (active[(size_t) k])
                db += coeffs[(size_t) k].magnitudeDb (f, rate);
        cutDb[i] = (float) -db;
    }
}

BandArray BellBank::response (const BellArray& gains) const noexcept
{
    BandArray out {};
    for (int b = 0; b < kNumBands; ++b)
    {
        float sum = 0.0f;
        for (int k = 0; k < count; ++k)
            sum += shape[(size_t) (b * oversample)][(size_t) k] * gains[(size_t) k];
        out[(size_t) b] = sum;
    }
    return out;
}

} // namespace ff
