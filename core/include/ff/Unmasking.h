// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ff/Bands.h"

namespace ff
{

//==============================================================================
/** Settings for the audible-masking model. */
struct MaskingSettings
{
    float sensitivityDb       = 6.0f;    ///< how far above the target's masking threshold the reference should sit
    float sideContributionDb  = -6.0f;   ///< a centred reference is masked less by the target's Side signal
    float upwardSlopeDb       = 12.0f;   ///< masking spread towards higher bands, dB per band
    float downwardSlopeDb     = 24.0f;   ///< masking spread towards lower bands, dB per band
    float referenceFloorDb    = -70.0f;  ///< reference bands quieter than this never ask for room
    float referenceRangeDb    = 45.0f;   ///< ...nor do bands this far below the reference's loudest band
    float rangeLowHz          = 20.0f;   ///< no release requested below this
    float rangeHighHz         = 20000.0f;///< no release requested above this
    float maxNeedDb           = 18.0f;
};

/** Computes, per band, how many dB of release the target track would need so that
    the reference stays audible above the target's masking threshold.

    The target's Mid and (attenuated) Side power form the masker; it is spread
    across bands with asymmetric slopes; the reference's Mid level is compared
    against that threshold. Only bands where the reference is actually present
    ask for room. */
BandArray computeNeed (const BandArray& targetMidDb,
                       const BandArray& targetSideDb,
                       const BandArray& referenceMidDb,
                       const MaskingSettings& settings) noexcept;

//==============================================================================
/** How the release is shared between the Space module and the EQ module. */
struct BudgetSettings
{
    float amount         = 0.7f;    ///< 0..2: how much of the needed release to apply
    float spaceAmount    = 1.0f;    ///< 0..2: 1 = the plugin's automatic choice
    bool  link           = true;    ///< EQ fills in what Space does not do
    float eqDepthDb      = 6.0f;    ///< maximum EQ cut per band
    float monoSafetyDb   = 6.0f;    ///< maximum Mid reduction Space may cause per band
    float bassMonoHz     = 150.0f;  ///< Space is disabled below this
    float autoSpaceShare = 0.75f;   ///< fraction Space takes at spaceAmount = 1
};

struct Budget
{
    BandArray total {}, space {}, eq {}, rest {};
};

/** Splits the per-band need into Space and EQ, as described in the spec's
    "release budget" section. */
Budget splitBudget (const BandArray& needDb, const BudgetSettings& settings) noexcept;

//==============================================================================
/** Per-band attack/release smoothing in the dB domain, run once per analysis frame. */
/** The same masking need at fine resolution (narrow Sharp mode): where exactly inside the
    analysis bands the reference is masked. The masker spreads with the same slopes per ERB
    as computeNeed(), and the reference's narrow-band power is scaled to a coarse band's
    width, so levels compare like the coarse ones. Only the shape is used (to place narrow bells); the depth still
    comes from the release budget. */
FineArray computeFineNeed (const FineArray& targetMidDb,
                           const FineArray& targetSideDb,
                           const FineArray& referenceMidDb,
                           const MaskingSettings& settings) noexcept;

class BandSmoother
{
public:
    void prepare (double frameRateHz) noexcept   { frameRate = frameRateHz; setSpeed (speed); }
    void reset (float value = 0.0f) noexcept     { state.fill (value); }

    /** 0 = "Rider" (slow, phrase-level), 1 = "Duck" (fast). */
    void setSpeed (float newSpeed) noexcept;

    /** Manual times override the Speed-derived ones; a value <= 0 means "auto" (follow Speed). */
    void setManualTimes (float attackMs, float releaseMs) noexcept;

    const BandArray& process (const BandArray& target) noexcept;
    const BandArray& value() const noexcept      { return state; }

    float attackMs() const noexcept   { return attack; }
    float releaseMs() const noexcept  { return release; }

private:
    void updateCoefficients() noexcept;

    double frameRate = 93.75;
    float speed = 0.3f, attack = 60.0f, release = 600.0f;
    float manualAttack = 0.0f, manualRelease = 0.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    BandArray state {};
};

} // namespace ff
