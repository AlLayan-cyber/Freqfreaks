// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ff/Analyzer.h"
#include "ff/StereoProcessor.h"
#include "ff/Unmasking.h"

#include <vector>

namespace ff
{

/** Everything the Track engine needs to know per block. */
struct TrackParams
{
    float amount        = 0.7f;     ///< 0..1 (the knob's 0..100 %)
    float spaceAmount   = 1.0f;     ///< 0..2 (the knob's 0..200 %)
    bool  link          = true;
    float speed         = 0.3f;     ///< 0 = Rider, 1 = Duck
    float eqDepthDb     = 6.0f;
    float monoSafetyDb  = 6.0f;
    float bassMonoHz    = 150.0f;
    float rangeLowHz    = 60.0f;
    float rangeHighHz   = 14000.0f;
    float sensitivityDb = 6.0f;
    SpaceMode spaceMode = SpaceMode::spread;
    float panSign       = 1.0f;     ///< Pan Away direction: +1 left, -1 right
    bool  bypass        = false;

    float outputDb      = 0.0f;     ///< manual output trim, -12..+12 dB
    bool  autoGain      = false;    ///< restore the loudness the release took away (up to +6 dB)
    bool  spaceComp     = true;     ///< keep loudness constant while Space Amount changes

    float hubAmount     = 1.0f;     ///< multipliers sent by the Hub (1 = no change)
    float hubSpace      = 1.0f;
};

/** The reference track's latest analysis, read from the shared bus. */
struct ReferenceSnapshot
{
    bool valid = false;
    BandArray midDb {};
};

/** The complete per-track signal chain:
    analysis -> masking need -> smoothing -> budget -> bell fit -> lookahead delay -> Space + EQ.

    The analysis looks at the undelayed input while the audio is delayed by the
    lookahead, so cuts start before the reference's attack arrives. */
class TrackEngine
{
public:
    static constexpr double kLookaheadSeconds = 0.030;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    int latencySamples() const noexcept                 { return lookahead; }

    void process (float* left, float* right, int numSamples,
                  const TrackParams& params, const ReferenceSnapshot& reference) noexcept;

    // Read-outs for the UI and the shared bus (updated once per analysis frame).
    const BandArray& ownMidDb() const noexcept          { return analyzer.midDb(); }
    const BandArray& ownSideDb() const noexcept         { return analyzer.sideDb(); }
    const Budget& budget() const noexcept               { return currentBudget; }
    const BellArray& eqBells() const noexcept           { return eqGains; }
    const BellArray& spaceBells() const noexcept        { return spaceGains; }
    const StereoProcessor& processor() const noexcept   { return stereo; }
    bool hasReference() const noexcept                  { return referenceActive; }

    /** Automatic part of the output gain right now (Space compensation or Auto), dB. */
    float compensationDb() const noexcept               { return compTargetDb; }

    /** Perceived loudness change (dB) of the processed track against its input,
        and against the same release with Space Amount at 0 %; exposed for tests. */
    float loudnessChangeDb() const noexcept             { return lossVsInputDb; }
    float spaceLoudnessDb() const noexcept              { return spaceVsAllEqDb; }

private:
    void updateControl (const TrackParams& params, const ReferenceSnapshot& reference) noexcept;
    void updateCompensation (const TrackParams& params) noexcept;

    double rate = 48000.0;
    Analyzer analyzer;
    BandSmoother smoother;
    StereoProcessor stereo;
    Budget currentBudget;
    BellArray eqGains {}, spaceGains {}, transferWeights {};
    bool referenceActive = false;

    float compTargetDb = 0.0f, lossVsInputDb = 0.0f, spaceVsAllEqDb = 0.0f;
    float outputGain = 1.0f, outputGainTarget = 1.0f, gainCoeff = 0.999f;

    int lookahead = 0, delayPos = 0;
    std::vector<float> delayLeft, delayRight;
};

} // namespace ff
