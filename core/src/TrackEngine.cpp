// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/TrackEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ff
{

void TrackEngine::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    analyzer.prepare (rate);
    smoother.prepare (analyzer.frameRateHz());
    paramCoeff = (float) std::exp (-1.0 / (0.060 * analyzer.frameRateHz()));   // ~60 ms knob smoothing
    for (int f = 0; f < kSharpSteps; ++f)
        chains[(size_t) f].prepare (rate, f);
    fadeLength = std::max (1, (int) std::lround (0.050 * rate));
    finePerBand.fill (0);
    for (int f = 0; f < kFineBands; ++f)
        ++finePerBand[(size_t) FineLayout::get().coarseBand[(size_t) f]];
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
    work = {};
    fineSmoothed.fill (0.0f);
    peakTargets = {};
    for (int f = 0; f < kSharpSteps; ++f)
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
    // Work left from the previous frame (only with very large blocks) is finished first.
    runWork (1 << 20);

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

    // Sharp: switch to another filter chain with a short cross-fade (one switch at a time).
    // The new chain is fitted from scratch over the next blocks, then fades in.
    const int wanted = std::clamp (p.sharpStep, 0, kSharpSteps - 1);
    if (wanted != active && fadeFrom < 0)
    {
        auto& next = chains[(size_t) wanted];
        next.reset();
        eqGains[(size_t) wanted].fill (0.0f);
        spaceGains[(size_t) wanted].fill (0.0f);
        transferWeights[(size_t) wanted].fill (0.0f);
        startWork (wanted, p, 4, true);
    }
    else if (active == 0)
    {
        fitBells (active, p, 1);   // Sharp 0: at once, exactly as before
    }
    else
    {
        startWork (active, p, 1, false);
    }

    // Narrow Sharp mode: moving narrow bells on the conflicts (off: they fade out).
    if (isNarrowSharp (p.sharp) || std::any_of (peakTargets.begin(), peakTargets.end(), [] (const PeakBell& b) { return b.cutDb > 0.0f; }))
    {
        updatePeaks (p, reference);
        chains[(size_t) active].setPeaks (peakTargets);
    }

    updateCompensation (p);
    for (auto& c : chains)
        c.setListen (p.listen);
    runWork (1);
}

void TrackEngine::updatePeaks (const TrackParams& p, const ReferenceSnapshot& reference) noexcept
{
    const bool on = isNarrowSharp (p.sharp) && referenceActive && reference.hasFine && ! p.bypass;
    const auto& fine = FineLayout::get();

    // Where exactly the conflict is: the masking need at fine resolution, smoothed in time
    // with the same attack and release as the coarse release.
    FineArray need {};
    if (on)
    {
        MaskingSettings ms;
        ms.sensitivityDb = p.sensitivityDb;
        ms.rangeLowHz = p.rangeLowHz;
        ms.rangeHighHz = p.rangeHighHz;
        need = computeFineNeed (analyzer.fineMidDb(), analyzer.fineSideDb(), reference.fineMidDb, ms);
    }
    const double frameRate = analyzer.frameRateHz();
    const float attack = (float) std::exp (-1.0 / (std::max (1.0f, smoother.attackMs()) * 0.001 * frameRate));
    const float release = (float) std::exp (-1.0 / (std::max (1.0f, smoother.releaseMs()) * 0.001 * frameRate));
    for (int f = 0; f < kFineBands; ++f)
    {
        auto& s = fineSmoothed[(size_t) f];
        const float x = need[(size_t) f];
        s = x + (x > s ? attack : release) * (s - x);
    }

    // Candidates: local peaks of the fine need, inside Range, in bands where the budget
    // gives EQ release. Depth = that band's EQ release, more where the need concentrates
    // (a narrow cut removes less, so it may go a little deeper), at most EQ Depth.
    std::array<float, kNumBands> fineMean {};
    for (int f = 0; f < kFineBands; ++f)
        fineMean[(size_t) fine.coarseBand[(size_t) f]] += fineSmoothed[(size_t) f];
    for (int b = 0; b < kNumBands; ++b)
        fineMean[(size_t) b] /= (float) std::max (1, finePerBand[(size_t) b]);

    std::array<std::pair<float, int>, kFineBands> candidates {};
    int numCandidates = 0;
    if (on)
        for (int f = 1; f < kFineBands - 1; ++f)
        {
            const float v = fineSmoothed[(size_t) f];
            const float hz = fine.centreHz[(size_t) f];
            if (v < 0.3f || v < fineSmoothed[(size_t) f - 1] || v <= fineSmoothed[(size_t) f + 1]
                || hz < p.rangeLowHz * 1.05f || hz > p.rangeHighHz / 1.05f)
                continue;
            const int b = fine.coarseBand[(size_t) f];
            const float eq = currentBudget.eq[(size_t) b];
            if (eq < 0.2f)
                continue;
            const float concentration = std::clamp (v / std::max (0.1f, fineMean[(size_t) b]), 1.0f, 2.0f);
            candidates[(size_t) numCandidates++] = { std::min (p.eqDepthDb, eq * concentration), f };
        }
    std::sort (candidates.begin(), candidates.begin() + numCandidates,
               [] (const auto& a, const auto& b) { return a.first > b.first; });

    // Keep the strongest, at least a third of an octave apart.
    std::array<std::pair<float, float>, kNumPeaks> chosen {};   // (cut, hz)
    int numChosen = 0;
    for (int c = 0; c < numCandidates && numChosen < kNumPeaks; ++c)
    {
        // Exact place: a parabola through the reference's level in the band and its neighbours
        // (the reference is what must come through), between the two neighbouring centres.
        const int f = candidates[(size_t) c].second;
        const float a0 = reference.fineMidDb[(size_t) f - 1], a1 = reference.fineMidDb[(size_t) f], a2 = reference.fineMidDb[(size_t) f + 1];
        const float denom = a0 - 2.0f * a1 + a2;
        const float offset = denom < -1.0e-3f ? std::clamp (0.5f * (a0 - a2) / denom, -0.5f, 0.5f) : 0.0f;
        const float hz = fine.centreHz[(size_t) f] * std::exp2 (offset * std::log2 (fine.centreHz[1] / fine.centreHz[0]));
        bool clear = true;
        for (int k = 0; k < numChosen; ++k)
            clear = clear && std::abs (std::log2 (hz / chosen[(size_t) k].second)) >= 1.0f / 3.0f;
        if (clear)
            chosen[(size_t) numChosen++] = { candidates[(size_t) c].first, hz };
    }

    // Each conflict keeps its bell from frame to frame (nearest bell within ~1/3 octave), so
    // bells glide after a moving conflict instead of jumping; new ones take a silent bell.
    const float q = narrowQFor (p.sharp);
    const auto& now = chains[(size_t) active].currentPeaks();
    PeakArray next = peakTargets;
    for (auto& b : next)
        b.cutDb = 0.0f, b.q = q;
    std::array<bool, kNumPeaks> taken {};
    std::array<bool, kNumPeaks> placed {};
    for (int c = 0; c < numChosen; ++c)
    {
        const float hz = chosen[(size_t) c].second;
        int best = -1;
        float bestDist = 0.34f;
        for (int k = 0; k < kNumPeaks; ++k)
        {
            if (taken[(size_t) k] || peakTargets[(size_t) k].cutDb <= 0.0f)
                continue;
            const float d = std::abs (std::log2 (hz / peakTargets[(size_t) k].hz));
            if (d < bestDist)
                bestDist = d, best = k;
        }
        if (best >= 0)
        {
            taken[(size_t) best] = true;
            next[(size_t) best].hz = hz;
            next[(size_t) best].cutDb = chosen[(size_t) c].first;
            placed[(size_t) c] = true;
        }
    }
    for (int c = 0; c < numChosen; ++c)
    {
        if (placed[(size_t) c])
            continue;
        for (int k = 0; k < kNumPeaks; ++k)
            if (! taken[(size_t) k] && peakTargets[(size_t) k].cutDb <= 0.0f && now[(size_t) k].cutDb < 0.05f)
            {
                taken[(size_t) k] = true;
                next[(size_t) k].hz = chosen[(size_t) c].second;
                next[(size_t) k].cutDb = chosen[(size_t) c].first;
                break;
            }
    }
    peakTargets = next;
}

void TrackEngine::startWork (int chain, const TrackParams& p, int passes, bool switching) noexcept
{
    work.chain = chain;
    work.stage = 0;
    work.stages = 4 * passes;
    work.switching = switching;
    work.params = p;
}

void TrackEngine::runWork (int maxStages) noexcept
{
    for (int done = 0; done < maxStages && work.chain >= 0; ++done)
    {
        const auto& p = work.params;
        const auto& bank = chains[(size_t) work.chain].bank();
        auto& eq = eqGains[(size_t) work.chain];
        auto& space = spaceGains[(size_t) work.chain];
        auto& transfer = transferWeights[(size_t) work.chain];
        const float spaceLowHz = std::max (p.bassMonoHz, p.rangeLowHz);

        // One pass = EQ fit, Space fit, and the transfer fit in two halves: four similar-sized stages.
        switch (work.stage % 4)
        {
            case 0:
                if (isNarrowSharp (p.sharp))
                    break;   // narrow mode: the EQ part is done by the moving bells
                bank.fit (currentBudget.eq, BellBank::rangeWeights (p.rangeLowHz, p.rangeHighHz),
                          bank.bellsWithin (p.rangeLowHz * 1.1f, p.rangeHighHz / 1.1f), p.eqDepthDb, eq);
                break;
            case 1:
                bank.fit (currentBudget.space, BellBank::rangeWeights (spaceLowHz, p.rangeHighHz),
                          bank.bellsWithin (spaceLowHz * 1.25f, p.rangeHighHz / 1.1f), p.monoSafetyDb, space);
                break;
            default:
                bank.fitTransfer (space, transfer, 8);
                break;
        }

        if (++work.stage >= work.stages)
            commitWork();
    }
}

void TrackEngine::commitWork() noexcept
{
    const auto& p = work.params;
    auto& stereo = chains[(size_t) work.chain];
    if (p.bypass)
    {
        BellArray zero {};
        stereo.setTargets (zero, zero, zero, p.spaceMode, p.panSign);
    }
    else
    {
        BellArray zero {};
        stereo.setTargets (isNarrowSharp (p.sharp) ? zero : eqGains[(size_t) work.chain], spaceGains[(size_t) work.chain],
                           transferWeights[(size_t) work.chain], p.spaceMode, p.panSign);
    }

    if (work.switching && work.chain != active && fadeFrom < 0)
    {
        stereo.setListen (p.listen);
        stereo.snapToTargets();
        fadeFrom = active;
        active = work.chain;
        fadePos = 0;
    }
    work.chain = -1;
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
    else
        runWork (1);   // the rest of a Sharp fit, one stage per block

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
