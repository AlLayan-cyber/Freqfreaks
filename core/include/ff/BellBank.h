// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ff/Bands.h"

#include <array>
#include <complex>

namespace ff
{

constexpr int kNumBells = 16;
using BellArray = std::array<float, kNumBells>;

/** A fixed bank of smooth, overlapping bell filters, and a fitter that turns a
    per-band cut curve into bell gains.

    This is the "parametric approximation" from the spec: the analysis works on
    32 bands, but audio is shaped by smooth bells. Bells sit at fixed, evenly
    log-spaced frequencies (about 1.8 per octave, so the bass is resolved as well
    as the highs); only their gains move, and the fit is warm-started from the
    previous frame, which keeps it stable in time.

    The fit is weighted: bands outside the allowed range (Range, Bass Mono) carry
    a large weight with a target of 0 dB, so a bell near a limit cuts less rather
    than leaking its skirt across it. */
class BellBank
{
public:
    void prepare (double sampleRate);

    double sampleRate() const noexcept               { return rate; }
    float centreHz (int bell) const noexcept         { return fc[(size_t) bell]; }
    float q (int bell) const noexcept                { return qs[(size_t) bell]; }

    /** Fits non-negative cuts (dB) so the bank's response approximates cutDb,
        minimising sum of weight[b] * error[b]^2. Bells with allowed[k] == false
        stay at 0. gains is used as warm start and receives the result. */
    void fit (const BandArray& cutDb, const BandArray& weight, const std::array<bool, kNumBells>& allowed,
              float maxCutDb, BellArray& gains) const noexcept;

    /** Weights for fit(): 1 inside [lowHz, highHz], outsideWeight elsewhere. */
    static BandArray rangeWeights (float lowHz, float highHz, float outsideWeight = 40.0f) noexcept;

    /** Bells whose centre lies inside [lowHz, highHz]; the rest would mostly cut outside. */
    std::array<bool, kNumBells> bellsWithin (float lowHz, float highHz) const noexcept;

    /** Exact cut (positive dB) of the whole bank at any frequency, for drawing. */
    float cutAt (float hz, const BellArray& gains) const noexcept;

    /** Exact cut (positive dB) at many frequencies at once (designs each bell once). */
    void cutCurve (const BellArray& gains, const float* hz, float* cutDb, int numPoints) const noexcept;

    /** Weights for the Space transfer band-passes (one per bell, 0 dB peak, same Q).

        Space cuts the Mid with the space bells (response H) and sends what it removed
        to the Side through the band-passes: T(f) = sum_k w_k BP_k(f). Energy is kept
        when |T(f)|^2 = 1 - |H(f)|^2 at every frequency. The band-passes overlap and
        add coherently, so the weights are fitted to that target on a fine grid
        (warm-started from the previous call). */
    void fitTransfer (const BellArray& spaceCutDb, BellArray& weights) const noexcept;

    /** |T(f)|^2 / (1 - |H(f)|^2) error report for tests: worst ratio in dB over the grid
        where at least minMovedDb of energy is moved. */
    float transferErrorDb (const BellArray& spaceCutDb, const BellArray& weights, float minMovedDb = 0.5f) const noexcept;

    /** Predicted cut (positive dB) in each analysis band for the given bell cuts. */
    BandArray response (const BellArray& gains) const noexcept;

private:
    float largestEigen (const BandArray& weight) const noexcept;
    float cachedEigen (const BandArray& weight) const noexcept;

    // The step of the weighted fit depends only on the weights, which change only when
    // Range / Bass Mono change: keep the last two (EQ and Space) instead of recomputing.
    struct EigenCache { BandArray weight {}; float value = 0.0f; };
    mutable std::array<EigenCache, 2> eigenCache {};
    mutable int eigenNext = 0;

    double rate = 48000.0;
    BellArray fc {}, qs {};
    std::array<std::array<float, kNumBells>, kNumBands> shape {};   // cut in band b per dB of bell k

    static constexpr int kGrid = 32;                                 // log grid for the transfer fit
    std::array<std::array<float, kNumBells>, kGrid> gridShape {};    // cut at grid point j per dB of bell k
    std::array<std::array<std::complex<float>, kNumBells>, kGrid> gridBandpass {};
    float transferStep = 0.1f;
};

} // namespace ff
