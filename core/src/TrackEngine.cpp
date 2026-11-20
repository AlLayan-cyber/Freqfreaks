// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/TrackEngine.h"

#include <algorithm>
#include <cmath>

namespace ff
{

void TrackEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    analyzer.prepare (rate);
    smoother.prepare (analyzer.frameRateHz());
    stereo.prepare (rate);

    lookahead = (int) std::lround (kLookaheadSeconds * rate);
    delayLeft.assign ((size_t) lookahead + 1, 0.0f);
    delayRight.assign ((size_t) lookahead + 1, 0.0f);
    reset();
}

void TrackEngine::reset() noexcept
{
    analyzer.reset();
    smoother.reset();
    stereo.reset();
    currentBudget = {};
    eqGains.fill (0.0f);
    spaceGains.fill (0.0f);
    std::fill (delayLeft.begin(), delayLeft.end(), 0.0f);
    std::fill (delayRight.begin(), delayRight.end(), 0.0f);
    delayPos = 0;
    referenceActive = false;
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
    const auto& smoothed = smoother.process (need);

    BudgetSettings bs;
    bs.amount = std::clamp (p.amount * p.hubAmount, 0.0f, 2.0f);
    bs.spaceAmount = std::clamp (p.spaceAmount * p.hubSpace, 0.0f, 2.0f);
    bs.link = p.link;
    bs.eqDepthDb = p.eqDepthDb;
    bs.monoSafetyDb = p.monoSafetyDb;
    bs.bassMonoHz = p.bassMonoHz;
    currentBudget = splitBudget (smoothed, bs);

    // Range and Bass Mono are hard limits: bells outside are off, and the fit is weighted so
    // that a bell near a limit cuts less instead of leaking its skirt across it.
    const auto& bank = stereo.bank();
    const float spaceLowHz = std::max (p.bassMonoHz, p.rangeLowHz);
    const auto eqWeights = BellBank::rangeWeights (p.rangeLowHz, p.rangeHighHz);
    const auto spaceWeights = BellBank::rangeWeights (spaceLowHz, p.rangeHighHz);
    const auto eqAllowed = bank.bellsWithin (p.rangeLowHz * 1.1f, p.rangeHighHz / 1.1f);
    const auto spaceAllowed = bank.bellsWithin (spaceLowHz * 1.25f, p.rangeHighHz / 1.1f);

    bank.fit (currentBudget.eq, eqWeights, eqAllowed, p.eqDepthDb, eqGains);
    bank.fit (currentBudget.space, spaceWeights, spaceAllowed, p.monoSafetyDb, spaceGains);

    if (p.bypass)
    {
        BellArray zero {};
        stereo.setTargets (zero, zero, p.spaceMode, p.panSign);
    }
    else
    {
        stereo.setTargets (eqGains, spaceGains, p.spaceMode, p.panSign);
    }
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

    stereo.process (left, right, numSamples);
}

} // namespace ff
