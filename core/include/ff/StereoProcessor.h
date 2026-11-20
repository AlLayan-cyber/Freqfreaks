// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ff/BellBank.h"
#include "ff/Biquad.h"

#include <array>
#include <vector>

namespace ff
{

enum class SpaceMode { spread = 0, panAway = 1, decorrelate = 2 };

/** Schroeder all-pass chain used to turn the moved Mid energy into a signal that
    is uncorrelated with the original, so it widens instead of panning. */
class Decorrelator
{
public:
    void prepare (double sampleRate, const std::array<float, 4>& delaysMs);
    void reset() noexcept;
    float process (float x) noexcept;

private:
    struct Stage
    {
        std::vector<float> buffer;
        int pos = 0;
    };
    std::array<Stage, 4> stages;
    float g = 0.5f;
};

/** Applies the Space module (Mid -> Side transfer per bell) and the EQ module
    (cuts per bell on both channels) to stereo audio.

    Space: the Mid signal is cut by the space bells; the removed energy is sent
    back as Side through matching band-passes, weighted so the total energy at
    every frequency stays the same (see BellBank::fitTransfer). Spread/Decorrelate pass that energy through a
    decorrelator (wider image); Pan Away keeps it coherent (moves the band left
    or right). Then the EQ bells cut both channels.

    Bell gains glide to their targets in small sub-blocks, so automation and
    analysis updates never click. Real-time safe after prepare(). */
class StereoProcessor
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    const BellBank& bank() const noexcept   { return bells; }

    /** Targets in positive dB of cut. transferWeights come from BellBank::fitTransfer().
        panSign: +1 moves energy left, -1 right. */
    void setTargets (const BellArray& eqCutDb, const BellArray& spaceCutDb, const BellArray& transferWeights,
                     SpaceMode mode, float panSign) noexcept;

    void process (float* left, float* right, int numSamples) noexcept;

    const BellArray& currentEqCut() const noexcept     { return eqNow; }
    const BellArray& currentSpaceCut() const noexcept  { return spaceNow; }

private:
    void updateCoefficients() noexcept;

    static constexpr int kSubBlock = 32;

    double rate = 48000.0;
    BellBank bells;
    Decorrelator shortDecorrelator, longDecorrelator;

    BellArray eqTarget {}, spaceTarget {}, transferTarget {}, eqNow {}, spaceNow {}, transferNow {};
    SpaceMode mode = SpaceMode::spread;
    float panSign = 1.0f, glideCoeff = 0.9f;

    std::array<BiquadCoeffs, kNumBells> eqCoeffs {}, spaceCoeffs {}, bandpassCoeffs {};
    // A bell at 0 dB is skipped entirely: bit-exact pass-through and less CPU.
    std::array<bool, kNumBells> eqActive {}, spaceActive {}, transferActive {};
    std::array<BiquadState, kNumBells> eqLeft {}, eqRight {}, spaceMid {}, transferBand {};
};

} // namespace ff
