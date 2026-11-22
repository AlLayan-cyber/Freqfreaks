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
    float attackMs      = 0.0f;     ///< manual attack; <= 0 = auto (from Speed)
    float releaseMs     = 0.0f;     ///< manual release; <= 0 = auto (from Speed)
    float eqDepthDb     = 6.0f;
    float monoSafetyDb  = 6.0f;
    float bassMonoHz    = 150.0f;
    float rangeLowHz    = 60.0f;
    float rangeHighHz   = 14000.0f;
    float sensitivityDb = 6.0f;
    SpaceMode spaceMode = SpaceMode::spread;
    float panSign       = 1.0f;     ///< Pan Away direction: +1 left, -1 right
    bool  bypass        = false;
    int   sharpStep     = 0;        ///< Sharp knob step (see sharpStepFor); 0 = the original sound
    Listen listen       = Listen::normal;

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
    const BellArray& eqBells() const noexcept           { return eqGains[(size_t) active]; }
    const BellArray& spaceBells() const noexcept        { return spaceGains[(size_t) active]; }
    const StereoProcessor& processor() const noexcept   { return chains[(size_t) active]; }
    int sharpStep() const noexcept                      { return active; }
    bool isSharpFading() const noexcept                 { return fadeFrom >= 0 || work.chain >= 0; }
    bool hasReference() const noexcept                  { return referenceActive; }

    /** The attack and release times in use right now (manual or from Speed), ms. */
    float attackMs() const noexcept                     { return smoother.attackMs(); }
    float releaseMs() const noexcept                    { return smoother.releaseMs(); }

    /** Automatic part of the output gain right now (Space compensation or Auto), dB. */
    float compensationDb() const noexcept               { return compTargetDb; }

    /** Perceived loudness change (dB) of the processed track against its input,
        and against the same release with Space Amount at 0 %; exposed for tests. */
    float loudnessChangeDb() const noexcept             { return lossVsInputDb; }
    float spaceLoudnessDb() const noexcept              { return spaceVsAllEqDb; }

private:
    void updateControl (const TrackParams& params, const ReferenceSnapshot& reference) noexcept;
    void updateCompensation (const TrackParams& params) noexcept;
    void fitBells (int chain, const TrackParams& params, int passes) noexcept;
    void startWork (int chain, const TrackParams& params, int passes, bool switching) noexcept;
    void runWork (int maxStages) noexcept;
    void commitWork() noexcept;
    void processChains (float* left, float* right, int numSamples) noexcept;

    double rate = 48000.0;
    Analyzer analyzer;
    BandSmoother smoother;
    // One filter chain per Sharp step. Switching cross-fades from the old chain to the
    // new one in ~50 ms (both run only during the fade).
    std::array<StereoProcessor, kSharpSteps> chains;
    std::array<BellArray, kSharpSteps> eqGains {}, spaceGains {}, transferWeights {};

    // Above Sharp 0 the bell fit is heavier, so it is spread over the next few audio blocks
    // (one stage per block) instead of landing in one block: no CPU peaks. At Sharp 0 the fit
    // runs at once, exactly as before.
    struct Work
    {
        int chain = -1;          // -1 = nothing pending
        int stage = 0, stages = 0;
        bool switching = false;  // preparing a new chain: snap and cross-fade when done
        TrackParams params;
    } work;
    int active = 0, fadeFrom = -1, fadePos = 0, fadeLength = 2400;
    std::vector<float> fadeLeft, fadeRight;
    Budget currentBudget;
    bool referenceActive = false;

    float compTargetDb = 0.0f, lossVsInputDb = 0.0f, spaceVsAllEqDb = 0.0f;
    float amountNow = -1.0f, spaceNow = -1.0f, paramCoeff = 0.8f;   // smoothed knobs (control rate)
    float outputGain = 1.0f, outputGainTarget = 1.0f, gainCoeff = 0.999f;

    int lookahead = 0, delayPos = 0;
    std::vector<float> delayLeft, delayRight;
};

} // namespace ff
