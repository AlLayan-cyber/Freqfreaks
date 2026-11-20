// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/StereoProcessor.h"

#include <algorithm>
#include <cmath>

namespace ff
{

//==============================================================================
void Decorrelator::prepare (double sampleRate, const std::array<float, 4>& delaysMs)
{
    for (size_t i = 0; i < stages.size(); ++i)
    {
        const int len = std::max (1, (int) std::lround (delaysMs[i] * 0.001 * sampleRate));
        stages[i].buffer.assign ((size_t) len, 0.0f);
        stages[i].pos = 0;
    }
}

void Decorrelator::reset() noexcept
{
    for (auto& s : stages)
    {
        std::fill (s.buffer.begin(), s.buffer.end(), 0.0f);
        s.pos = 0;
    }
}

float Decorrelator::process (float x) noexcept
{
    for (auto& s : stages)
    {
        const float delayed = s.buffer[(size_t) s.pos];
        const float y = -g * x + delayed;
        s.buffer[(size_t) s.pos] = x + g * y;
        s.pos = s.pos + 1 < (int) s.buffer.size() ? s.pos + 1 : 0;
        x = y;
    }
    return x;
}

//==============================================================================
void StereoProcessor::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    bells.prepare (rate);
    shortDecorrelator.prepare (rate, { 1.13f, 1.71f, 2.39f, 3.07f });
    longDecorrelator.prepare (rate, { 3.71f, 5.29f, 7.93f, 11.27f });

    // ~15 ms glide per sub-block step.
    glideCoeff = (float) std::exp (-(double) kSubBlock / (0.015 * rate));

    for (int k = 0; k < kNumBells; ++k)
        bandpassCoeffs[(size_t) k] = BiquadCoeffs::bandpass (rate, bells.centreHz (k), bells.q (k));

    reset();
}

void StereoProcessor::reset() noexcept
{
    eqNow.fill (0.0f);
    spaceNow.fill (0.0f);
    transferNow.fill (0.0f);
    for (auto* bank : { &eqLeft, &eqRight, &spaceMid, &transferBand })
        for (auto& s : *bank)
            s.reset();
    shortDecorrelator.reset();
    longDecorrelator.reset();
    eqActive.fill (false);
    spaceActive.fill (false);
    transferActive.fill (false);
    updateCoefficients();
}

void StereoProcessor::setTargets (const BellArray& eqCutDb, const BellArray& spaceCutDb, const BellArray& transferWeights,
                                  SpaceMode newMode, float newPanSign) noexcept
{
    eqTarget = eqCutDb;
    spaceTarget = spaceCutDb;
    transferTarget = transferWeights;
    mode = newMode;
    panSign = newPanSign >= 0.0f ? 1.0f : -1.0f;
}

void StereoProcessor::updateCoefficients() noexcept
{
    constexpr float activeDb = 1.0e-3f;

    for (int k = 0; k < kNumBells; ++k)
    {
        const auto i = (size_t) k;

        // Switching a bell in starts from clean state; its gain is ~0 dB then, so there is no click.
        const bool eqOn = eqNow[i] > activeDb;
        if (eqOn && ! eqActive[i])
        {
            eqLeft[i].reset();
            eqRight[i].reset();
        }
        eqActive[i] = eqOn;

        const bool spaceOn = spaceNow[i] > activeDb;
        if (spaceOn && ! spaceActive[i])
            spaceMid[i].reset();
        spaceActive[i] = spaceOn;

        const bool transferOn = transferNow[i] > 1.0e-4f;
        if (transferOn && ! transferActive[i])
            transferBand[i].reset();
        transferActive[i] = transferOn;

        eqCoeffs[i]    = BiquadCoeffs::peaking (rate, bells.centreHz (k), bells.q (k), -eqNow[i]);
        spaceCoeffs[i] = BiquadCoeffs::peaking (rate, bells.centreHz (k), bells.q (k), -spaceNow[i]);
    }
}

void StereoProcessor::process (float* left, float* right, int numSamples) noexcept
{
    int done = 0;
    while (done < numSamples)
    {
        // Glide gains, then refresh coefficients for this sub-block.
        bool changed = false;
        for (int k = 0; k < kNumBells; ++k)
        {
            const auto i = (size_t) k;
            const float e = eqTarget[i] + glideCoeff * (eqNow[i] - eqTarget[i]);
            const float s = spaceTarget[i] + glideCoeff * (spaceNow[i] - spaceTarget[i]);
            const float t = transferTarget[i] + glideCoeff * (transferNow[i] - transferTarget[i]);
            changed = changed || std::abs (e - eqNow[i]) > 1.0e-4f || std::abs (s - spaceNow[i]) > 1.0e-4f
                      || std::abs (t - transferNow[i]) > 1.0e-5f;
            eqNow[i] = e;
            spaceNow[i] = s;
            transferNow[i] = t;
        }
        if (changed)
            updateCoefficients();

        const int n = std::min (kSubBlock, numSamples - done);
        for (int j = done; j < done + n; ++j)
        {
            const float m = 0.5f * (left[j] + right[j]);
            float side = 0.5f * (left[j] - right[j]);

            float mid = m;
            for (int k = 0; k < kNumBells; ++k)
                if (spaceActive[(size_t) k])
                    mid = spaceMid[(size_t) k].process (mid, spaceCoeffs[(size_t) k]);

            float source = m;
            if (mode == SpaceMode::spread)
                source = shortDecorrelator.process (m);
            else if (mode == SpaceMode::decorrelate)
                source = longDecorrelator.process (m);

            float moved = 0.0f;
            for (int k = 0; k < kNumBells; ++k)
                if (transferActive[(size_t) k])
                    moved += transferNow[(size_t) k] * transferBand[(size_t) k].process (source, bandpassCoeffs[(size_t) k]);

            side += mode == SpaceMode::panAway ? panSign * moved : moved;

            float l = mid + side;
            float r = mid - side;
            for (int k = 0; k < kNumBells; ++k)
            {
                if (eqActive[(size_t) k])
                {
                    l = eqLeft[(size_t) k].process (l, eqCoeffs[(size_t) k]);
                    r = eqRight[(size_t) k].process (r, eqCoeffs[(size_t) k]);
                }
            }
            left[j] = l;
            right[j] = r;
        }
        done += n;
    }
}

} // namespace ff
