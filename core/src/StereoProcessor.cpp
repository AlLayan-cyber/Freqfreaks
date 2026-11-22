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
void StereoProcessor::prepare (double sampleRate, int sharpStep)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    bells.prepare (rate, sharpStep);
    shortDecorrelator.prepare (rate, { 1.13f, 1.71f, 2.39f, 3.07f });
    longDecorrelator.prepare (rate, { 3.71f, 5.29f, 7.93f, 11.27f });

    // Gains glide with ~30 ms, modes cross-fade and Pan Away turns with ~40 ms: no step is ever abrupt.
    glideCoeff = (float) std::exp (-(double) kSubBlock / (0.030 * rate));
    modeCoeff = (float) std::exp (-(double) kSubBlock / (0.040 * rate));
    listenCoeff = (float) std::exp (-(double) kSubBlock / (0.020 * rate));

    for (int k = 0; k < bells.size(); ++k)
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
    modeMix = { 0.0f, 0.0f, 0.0f };
    modeMix[(size_t) mode] = 1.0f;
    panNow = panTarget;
    modeGainPrev = { modeMix[0], modeMix[1] * panNow, modeMix[2] };
    listenNow = listenPrev = listenTarget;
    updateCoefficients();
}

void StereoProcessor::snapToTargets() noexcept
{
    eqNow = eqTarget;
    spaceNow = spaceTarget;
    transferNow = transferTarget;
    modeMix = { 0.0f, 0.0f, 0.0f };
    modeMix[(size_t) mode] = 1.0f;
    panNow = panTarget;
    constexpr float halfPi = 1.5707963267948966f;
    modeGainPrev = { std::sin (halfPi * modeMix[0]), std::sin (halfPi * modeMix[1]) * panNow, std::sin (halfPi * modeMix[2]) };
    listenNow = listenPrev = listenTarget;
    updateCoefficients();
}

void StereoProcessor::setListen (Listen l) noexcept
{
    switch (l)
    {
        case Listen::deltaAll:   listenTarget = { 1.0f, 0.0f, -1.0f }; break;
        case Listen::deltaEq:    listenTarget = { 0.0f, 1.0f, -1.0f }; break;
        case Listen::deltaSpace: listenTarget = { 1.0f, -1.0f, 0.0f }; break;
        case Listen::input:      listenTarget = { 1.0f, 0.0f, 0.0f }; break;
        case Listen::normal:
        default:                 listenTarget = { 0.0f, 0.0f, 1.0f }; break;
    }
}

void StereoProcessor::setTargets (const BellArray& eqCutDb, const BellArray& spaceCutDb, const BellArray& transferWeights,
                                  SpaceMode newMode, float newPanSign) noexcept
{
    eqTarget = eqCutDb;
    spaceTarget = spaceCutDb;
    transferTarget = transferWeights;
    mode = newMode;
    panTarget = newPanSign >= 0.0f ? 1.0f : -1.0f;
}

void StereoProcessor::updateCoefficients() noexcept
{
    constexpr float activeDb = 1.0e-3f;

    for (int k = 0; k < bells.size(); ++k)
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

        for (int m = 0; m < 3; ++m)
        {
            const float target = m == (int) mode ? 1.0f : 0.0f;
            modeMix[(size_t) m] = target + modeCoeff * (modeMix[(size_t) m] - target);
        }
        panNow = panTarget + modeCoeff * (panNow - panTarget);
        for (size_t c = 0; c < 3; ++c)
        {
            listenNow[c] = listenTarget[c] + listenCoeff * (listenNow[c] - listenTarget[c]);
            if (std::abs (listenNow[c] - listenTarget[c]) < 1.0e-5f)
                listenNow[c] = listenTarget[c];
        }
        const bool listenPlain = listenNow == listenTarget && listenPrev == listenTarget
                                 && listenTarget[0] == 0.0f && listenTarget[1] == 0.0f;

        // Equal-power cross-fade (the three signals are mutually uncorrelated) with a sine law,
        // which starts with a finite slope, ramped per sample across the sub-block.
        constexpr float halfPi = 1.5707963267948966f;
        const std::array<float, 3> modeGainNext { std::sin (halfPi * modeMix[0]),
                                                  std::sin (halfPi * modeMix[1]) * panNow,
                                                  std::sin (halfPi * modeMix[2]) };

        const int n = std::min (kSubBlock, numSamples - done);
        const float rampStep = 1.0f / (float) n;
        for (int j = done; j < done + n; ++j)
        {
            const float t = (float) (j - done + 1) * rampStep;
            const float gSpread = modeGainPrev[0] + t * (modeGainNext[0] - modeGainPrev[0]);
            const float gPan    = modeGainPrev[1] + t * (modeGainNext[1] - modeGainPrev[1]);
            const float gDecor  = modeGainPrev[2] + t * (modeGainNext[2] - modeGainPrev[2]);

            const float dryL = left[j], dryR = right[j];
            const float m = 0.5f * (left[j] + right[j]);
            float side = 0.5f * (left[j] - right[j]);

            float mid = m;
            for (int k = 0; k < kNumBells; ++k)
                if (spaceActive[(size_t) k])
                    mid = spaceMid[(size_t) k].process (mid, spaceCoeffs[(size_t) k]);

            // What Space moved out of the Mid, band-limited by the transfer band-passes.
            float moved = 0.0f;
            for (int k = 0; k < kNumBells; ++k)
                if (transferActive[(size_t) k])
                    moved += transferNow[(size_t) k] * transferBand[(size_t) k].process (m, bandpassCoeffs[(size_t) k]);

            // The decorrelators are linear and time-invariant, so applying them after the
            // band-passes is the same as before; all three modes exist at once and can cross-fade.
            const float spread = shortDecorrelator.process (moved);
            const float decor = longDecorrelator.process (moved);
            side += gSpread * spread + gPan * moved + gDecor * decor;

            float l = mid + side;
            float r = mid - side;
            const float spacedL = l, spacedR = r;
            for (int k = 0; k < kNumBells; ++k)
            {
                if (eqActive[(size_t) k])
                {
                    l = eqLeft[(size_t) k].process (l, eqCoeffs[(size_t) k]);
                    r = eqRight[(size_t) k].process (r, eqCoeffs[(size_t) k]);
                }
            }
            if (listenPlain)
            {
                left[j] = l;
                right[j] = r;
            }
            else
            {
                // Delta / input monitoring, ramped per sample so switching never clicks.
                const float a = listenPrev[0] + t * (listenNow[0] - listenPrev[0]);
                const float b = listenPrev[1] + t * (listenNow[1] - listenPrev[1]);
                const float c = listenPrev[2] + t * (listenNow[2] - listenPrev[2]);
                left[j] = a * dryL + b * spacedL + c * l;
                right[j] = a * dryR + b * spacedR + c * r;
            }
        }
        modeGainPrev = modeGainNext;
        listenPrev = listenNow;
        done += n;
    }
}

} // namespace ff
