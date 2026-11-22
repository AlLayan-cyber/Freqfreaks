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

/** Narrow Sharp mode: up to this many moving narrow EQ bells, placed on the conflicts. */
constexpr int kNumPeaks = 8;

/** One moving narrow bell: centre, cut (positive dB) and Q. A cut of 0 = unused. */
struct PeakBell
{
    float hz = 1000.0f;
    float cutDb = 0.0f;
    float q = 4.0f;
};
using PeakArray = std::array<PeakBell, kNumPeaks>;

/** What the track's output carries: the processed sound, or what was taken away
    (Delta: everything, only the EQ part, only the Space part), or the input itself. */
enum class Listen { normal = 0, deltaAll = 1, deltaEq = 2, deltaSpace = 3, input = 4 };

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
    void prepare (double sampleRate, int sharpStep = 0);
    void reset() noexcept;

    /** Jumps every glide to its target at once (for a chain that is about to fade in). */
    void snapToTargets() noexcept;

    /** Delta / input monitoring; switches glide in ~20 ms. */
    void setListen (Listen) noexcept;

    const BellBank& bank() const noexcept   { return bells; }

    /** Targets in positive dB of cut. transferWeights come from BellBank::fitTransfer().
        panSign: +1 moves energy left, -1 right. */
    void setTargets (const BellArray& eqCutDb, const BellArray& spaceCutDb, const BellArray& transferWeights,
                     SpaceMode mode, float panSign) noexcept;

    /** Narrow bells on top of the EQ bells. Cuts glide like the others; a bell's centre
        glides too (in octaves, ~60 ms), so following a moving conflict never clicks. */
    void setPeaks (const PeakArray& peaks) noexcept      { peakTarget = peaks; }
    const PeakArray& currentPeaks() const noexcept      { return peakNow; }

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
    float panTarget = 1.0f, panNow = 1.0f, glideCoeff = 0.9f, modeCoeff = 0.9f, listenCoeff = 0.9f;

    // Output = dry * a + after-Space * b + after-EQ * c (Delta and input monitoring).
    std::array<float, 3> listenTarget { 0.0f, 0.0f, 1.0f }, listenNow { 0.0f, 0.0f, 1.0f }, listenPrev { 0.0f, 0.0f, 1.0f };

    // Mode crossfade: how much of the Spread / Pan Away / Decorrelate signal is used now.
    std::array<float, 3> modeMix { 1.0f, 0.0f, 0.0f };
    std::array<float, 3> modeGainPrev { 1.0f, 0.0f, 0.0f };   // per-sample ramps start here

    std::array<BiquadCoeffs, kNumBells> eqCoeffs {}, spaceCoeffs {}, bandpassCoeffs {};
    // A bell at 0 dB is skipped entirely: bit-exact pass-through and less CPU.
    std::array<bool, kNumBells> eqActive {}, spaceActive {}, transferActive {};
    std::array<BiquadState, kNumBells> eqLeft {}, eqRight {}, spaceMid {}, transferBand {};

    PeakArray peakTarget {}, peakNow {};
    std::array<BiquadCoeffs, kNumPeaks> peakCoeffs {};
    std::array<bool, kNumPeaks> peakActive {};
    std::array<BiquadState, kNumPeaks> peakLeft {}, peakRight {};
    float hzCoeff = 0.9f;
    bool anyPeak = false;
    void updatePeaks() noexcept;
};

} // namespace ff
