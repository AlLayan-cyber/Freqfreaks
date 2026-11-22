// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/TrackEngine.h"

#include <algorithm>
#include <cmath>

namespace ff
{

void TrackEngine::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    analyzer.prepare (rate);
    smoother.prepare (analyzer.frameRateHz());
    paramCoeff = (float) std::exp (-1.0 / (0.060 * analyzer.frameRateHz()));   // ~60 ms knob smoothing
    for (int f = 0; f < kNumFocus; ++f)
        chains[(size_t) f].prepare (rate, (Focus) f);
    fadeLength = std::max (1, (int) std::lround (0.050 * rate));
    fadeLeft.assign ((size_t) std::max (64, maxBlockSize), 0.0f);
    fadeRight.assign (fadeLeft.size(), 0.0f);

    gainCoeff = (float) std::exp (-1.0 / (0.15 * rate));   // ~150 ms output-gain glide
    lookahead = (int) std::lround (kLookaheadSeconds * rate);
    delayLeft.assign ((size_t) lookahead + 1, 0.0f);
    delayRight.assign ((size_t) lookahead + 1, 0.0f);
    reset();
}

void TrackEngine::reset() noexcept
{
    analyzer.reset();
    smoother.reset();
    for (auto& c : chains)
        c.reset();
    fadeFrom = -1;
    fadePos = 0;
    currentBudget = {};
    for (int f = 0; f < kNumFocus; ++f)
    {
        eqGains[(size_t) f].fill (0.0f);
        spaceGains[(size_t) f].fill (0.0f);
        transferWeights[(size_t) f].fill (0.0f);
    }
    std::fill (delayLeft.begin(), delayLeft.end(), 0.0f);
    std::fill (delayRight.begin(), delayRight.end(), 0.0f);
    delayPos = 0;
    referenceActive = false;
    compTargetDb = lossVsInputDb = spaceVsAllEqDb = 0.0f;
    amountNow = spaceNow = -1.0f;
    outputGain = outputGainTarget = 1.0f;
}

void TrackEngine::updateControl (const TrackParams& p, const ReferenceSnapshot& reference) noexcept
{
    MaskingSettings ms;
    ms.sensitivityDb = p.sensitivityDb;
    ms.rangeLowHz = p.rangeLowHz;
    ms.rangeHighHz = p.rangeHighHz;

    referenceActive = reference.valid && ! p.bypass;

    BandArray need {};
    if (referenceActive)
        need = computeNeed (analyzer.midDb(), analyzer.sideDb(), reference.midDb, ms);

    smoother.setSpeed (p.speed);
    smoother.setManualTimes (p.attackMs, p.releaseMs);
    const auto& smoothed = smoother.process (need);

    // Knob and Hub moves are smoothed here, before the budget: a jump of Amount or Space Amount
    // turns into a ~60 ms ramp instead of a step in every band at once.
    const float amountTarget = std::clamp (p.amount * p.hubAmount, 0.0f, 2.0f);
    const float spaceTarget = std::clamp (p.spaceAmount * p.hubSpace, 0.0f, 2.0f);
    amountNow = amountNow < 0.0f ? amountTarget : amountTarget + paramCoeff * (amountNow - amountTarget);
    spaceNow = spaceNow < 0.0f ? spaceTarget : spaceTarget + paramCoeff * (spaceNow - spaceTarget);

    BudgetSettings bs;
    bs.amount = amountNow;
    bs.spaceAmount = spaceNow;
    bs.link = p.link;
    bs.eqDepthDb = p.eqDepthDb;
    bs.monoSafetyDb = p.monoSafetyDb;
    bs.bassMonoHz = p.bassMonoHz;
    currentBudget = splitBudget (smoothed, bs);

    // Focus: switch to another filter chain with a short cross-fade (one switch at a time).
    const int wanted = std::clamp ((int) p.focus, 0, kNumFocus - 1);
    if (wanted != active && fadeFrom < 0)
    {
        fadeFrom = active;
        active = wanted;
        fadePos = 0;
        auto& next = chains[(size_t) active];
        next.reset();
        eqGains[(size_t) active].fill (0.0f);
        spaceGains[(size_t) active].fill (0.0f);
        transferWeights[(size_t) active].fill (0.0f);
        fitBells (active, p, 4);   // converge from scratch before it is heard (the fade hides the rest)
        next.setListen (p.listen);
        next.snapToTargets();
    }
    else
    {
        fitBells (active, p, 1);
    }

    updateCompensation (p);
    for (auto& c : chains)
        c.setListen (p.listen);
}

void TrackEngine::fitBells (int chain, const TrackParams& p, int passes) noexcept
{
    // Range and Bass Mono are hard limits: bells outside are off, and the fit is weighted so
    // that a bell near a limit cuts less instead of leaking its skirt across it.
    auto& stereo = chains[(size_t) chain];
    auto& eq = eqGains[(size_t) chain];
    auto& space = spaceGains[(size_t) chain];
    auto& transfer = transferWeights[(size_t) chain];
    const auto& bank = stereo.bank();
    const float spaceLowHz = std::max (p.bassMonoHz, p.rangeLowHz);
    const auto eqWeights = BellBank::rangeWeights (p.rangeLowHz, p.rangeHighHz);
    const auto spaceWeights = BellBank::rangeWeights (spaceLowHz, p.rangeHighHz);
    const auto eqAllowed = bank.bellsWithin (p.rangeLowHz * 1.1f, p.rangeHighHz / 1.1f);
    const auto spaceAllowed = bank.bellsWithin (spaceLowHz * 1.25f, p.rangeHighHz / 1.1f);

    for (int i = 0; i < passes; ++i)
    {
        bank.fit (currentBudget.eq, eqWeights, eqAllowed, p.eqDepthDb, eq);
        bank.fit (currentBudget.space, spaceWeights, spaceAllowed, p.monoSafetyDb, space);
        bank.fitTransfer (space, transfer);
    }

    if (p.bypass)
    {
        BellArray zero {};
        stereo.setTargets (zero, zero, zero, p.spaceMode, p.panSign);
    }
    else
    {
        stereo.setTargets (eq, space, transfer, p.spaceMode, p.panSign);
    }
}

void TrackEngine::processChains (float* left, float* right, int numSamples) noexcept
{
    if (fadeFrom < 0)
    {
        chains[(size_t) active].process (left, right, numSamples);
        return;
    }

    // Cross-fade: the old chain (frozen at its last targets) fades out, the new one fades in.
    // The two outputs are nearly identical, so an equal-gain raised-cosine fade keeps the level.
    const int chunk = (int) fadeLeft.size();
    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = std::min (chunk, numSamples - start);
        float* l = left + start;
        float* r = right + start;
        if (fadeFrom < 0)
        {
            chains[(size_t) active].process (l, r, n);
            continue;
        }
        std::copy (l, l + n, fadeLeft.begin());
        std::copy (r, r + n, fadeRight.begin());
        chains[(size_t) fadeFrom].process (fadeLeft.data(), fadeRight.data(), n);
        chains[(size_t) active].process (l, r, n);
        for (int i = 0; i < n; ++i)
        {
            const float t = std::min (1.0f, (float) fadePos / (float) fadeLength);
            const float g = 0.5f - 0.5f * std::cos (3.14159265f * t);
            l[i] = g * l[i] + (1.0f - g) * fadeLeft[(size_t) i];
            r[i] = g * r[i] + (1.0f - g) * fadeRight[(size_t) i];
            if (fadePos < fadeLength)
                ++fadePos;
        }
        if (fadePos >= fadeLength)
            fadeFrom = -1;
    }
}

void TrackEngine::updateCompensation (const TrackParams& p) noexcept
{
    // Model, not measurement: the loudness the track would have with the current split,
    // with the same release done by EQ alone, and without any release. A measured loop
    // would pump with the music; the model follows only the release itself.
    //
    // Space keeps energy but sounds a little louder, because wide sounds louder than narrow:
    // moved energy counts kWidthLoudness more. EQ removes energy.
    constexpr float kWidthLoudness = 0.25f;   // about +1 dB when a band moves to the sides completely
    const auto& layout = BandLayout::get();

    // Reference: the same release with Space Amount at 0 % (same Link setting).
    BudgetSettings noSpace;
    noSpace.amount = 1.0f;
    noSpace.spaceAmount = 0.0f;
    noSpace.link = p.link;
    noSpace.eqDepthDb = p.eqDepthDb;
    noSpace.monoSafetyDb = p.monoSafetyDb;
    noSpace.bassMonoHz = p.bassMonoHz;
    const auto reference = splitBudget (currentBudget.total, noSpace);

    double current = 0.0, allEq = 0.0, input = 0.0;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto i = (size_t) b;
        const float hz = layout.centreHz[i];

        // Rough equal-loudness weighting: the extreme lows and highs matter less.
        const float w = hz < 200.0f ? hz / 200.0f : (hz > 6000.0f ? std::sqrt (6000.0f / hz) : 1.0f);
        const double power = (double) w * ((double) dbToPower (analyzer.midDb()[i]) + (double) dbToPower (analyzer.sideDb()[i]));

        const float eqCut = currentBudget.eq[i];
        const float moved = 1.0f - dbToPower (-currentBudget.space[i]);
        const float eqOnly = reference.eq[i];

        input   += power;
        current += power * dbToPower (-eqCut) * (1.0f + kWidthLoudness * moved);
        allEq   += power * dbToPower (-eqOnly);
    }

    if (input < 1.0e-10 || current < 1.0e-12)
    {
        lossVsInputDb = spaceVsAllEqDb = 0.0f;
    }
    else
    {
        lossVsInputDb = (float) (10.0 * std::log10 (current / input));
        spaceVsAllEqDb = (float) (10.0 * std::log10 (current / std::max (allEq, 1.0e-12)));
    }

    if (p.bypass)
        compTargetDb = 0.0f;
    else if (p.autoGain)
        compTargetDb = std::clamp (-lossVsInputDb, 0.0f, 6.0f);     // back to the input's loudness
    else if (p.spaceComp)
        compTargetDb = std::clamp (-spaceVsAllEqDb, -6.0f, 0.0f);   // Space never makes it louder
    else
        compTargetDb = 0.0f;

    outputGainTarget = p.listen != Listen::normal ? 1.0f
                                                  : dbToGain (compTargetDb + std::clamp (p.outputDb, -12.0f, 12.0f));
}

void TrackEngine::process (float* left, float* right, int numSamples,
                           const TrackParams& params, const ReferenceSnapshot& reference) noexcept
{
    if (analyzer.process (left, right, numSamples) > 0)
        updateControl (params, reference);

    // Lookahead: delay the audio, not the analysis.
    const int len = (int) delayLeft.size();
    for (int i = 0; i < numSamples; ++i)
    {
        delayLeft[(size_t) delayPos] = left[i];
        delayRight[(size_t) delayPos] = right[i];
        const int readPos = delayPos + 1 < len ? delayPos + 1 : 0;
        left[i] = delayLeft[(size_t) readPos];
        right[i] = delayRight[(size_t) readPos];
        delayPos = readPos;
    }

    processChains (left, right, numSamples);

    // Output gain: manual trim plus compensation, glided so it never clicks.
    // Delta and input monitoring are heard without it, as they are.
    for (int i = 0; i < numSamples; ++i)
    {
        outputGain = outputGainTarget + gainCoeff * (outputGain - outputGainTarget);
        left[i] *= outputGain;
        right[i] *= outputGain;
    }
}

} // namespace ff
