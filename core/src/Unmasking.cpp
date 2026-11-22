// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/Unmasking.h"

#include <algorithm>
#include <cmath>

namespace ff
{

BandArray computeNeed (const BandArray& targetMidDb,
                       const BandArray& targetSideDb,
                       const BandArray& referenceMidDb,
                       const MaskingSettings& s) noexcept
{
    const auto& layout = BandLayout::get();
    const float sideFactor = dbToPower (s.sideContributionDb);

    std::array<double, kNumBands> maskerPower {};
    for (int b = 0; b < kNumBands; ++b)
        maskerPower[(size_t) b] = (double) dbToPower (targetMidDb[(size_t) b])
                                + (double) sideFactor * (double) dbToPower (targetSideDb[(size_t) b]);

    float refMax = kSilenceDb;
    for (auto v : referenceMidDb)
        refMax = std::max (refMax, v);
    const float presenceFloor = std::max (s.referenceFloorDb, refMax - s.referenceRangeDb);

    BandArray need {};
    for (int b = 0; b < kNumBands; ++b)
    {
        const float hz = layout.centreHz[(size_t) b];
        const float ref = referenceMidDb[(size_t) b];

        if (hz < s.rangeLowHz || hz > s.rangeHighHz || ref < presenceFloor)
        {
            need[(size_t) b] = 0.0f;
            continue;
        }

        double spread = 0.0;
        for (int j = 0; j < kNumBands; ++j)
        {
            const int d = b - j;
            const float attenuation = d >= 0 ? s.upwardSlopeDb * (float) d : s.downwardSlopeDb * (float) -d;
            if (attenuation < 90.0f)
                spread += maskerPower[(size_t) j] * std::pow (10.0, -attenuation / 10.0);
        }

        const float threshold = powerToDb ((float) spread);
        need[(size_t) b] = std::clamp (threshold + s.sensitivityDb - ref, 0.0f, s.maxNeedDb);
    }
    return need;
}

FineArray computeFineNeed (const FineArray& targetMidDb,
                           const FineArray& targetSideDb,
                           const FineArray& referenceMidDb,
                           const MaskingSettings& s) noexcept
{
    const auto& fine = FineLayout::get();

    // Spreading factors between fine bands, from the coarse slopes per ERB (the slopes are
    // the defaults in practice; recomputed only if they change).
    struct Spread
    {
        float up = -1.0f, down = -1.0f;
        std::array<std::array<float, kFineBands>, kFineBands> factor {};
        std::array<float, kFineBands> widthDb {};   // fine band width against a coarse band, dB
    };
    static thread_local Spread spread;
    if (spread.up != s.upwardSlopeDb || spread.down != s.downwardSlopeDb)
    {
        const auto& layout = BandLayout::get();
        const float erbPerBand = (hzToErbRate (layout.centreHz[kNumBands - 1]) - hzToErbRate (layout.centreHz[0]))
                                 / (float) (kNumBands - 1);
        for (int b = 0; b < kFineBands; ++b)
            for (int j = 0; j < kFineBands; ++j)
            {
                const float d = (fine.erbRate[(size_t) b] - fine.erbRate[(size_t) j]) / erbPerBand;   // in coarse bands
                const float attenuation = d >= 0.0f ? s.upwardSlopeDb * d : s.downwardSlopeDb * -d;
                spread.factor[(size_t) b][(size_t) j] = attenuation < 90.0f ? std::pow (10.0f, -attenuation / 10.0f) : 0.0f;
            }
        for (int b = 0; b < kFineBands; ++b)
        {
            const float width = (fine.erbRate[(size_t) std::min (b + 1, kFineBands - 1)]
                                 - fine.erbRate[(size_t) std::max (b - 1, 0)])
                                * (b == 0 || b == kFineBands - 1 ? 1.0f : 0.5f) / erbPerBand;
            spread.widthDb[(size_t) b] = 10.0f * std::log10 (std::max (width, 1.0e-3f));
        }
        spread.up = s.upwardSlopeDb;
        spread.down = s.downwardSlopeDb;
    }

    const float sideFactor = dbToPower (s.sideContributionDb);
    std::array<float, kFineBands> masker {};
    for (int j = 0; j < kFineBands; ++j)
        masker[(size_t) j] = dbToPower (targetMidDb[(size_t) j]) + sideFactor * dbToPower (targetSideDb[(size_t) j]);

    float refMax = kSilenceDb;
    for (auto v : referenceMidDb)
        refMax = std::max (refMax, v);
    const float presenceFloor = std::max (s.referenceFloorDb, refMax - s.referenceRangeDb);

    FineArray need {};
    for (int b = 0; b < kFineBands; ++b)
    {
        const float hz = fine.centreHz[(size_t) b];
        const float ref = referenceMidDb[(size_t) b];
        if (hz < s.rangeLowHz || hz > s.rangeHighHz || ref < presenceFloor)
            continue;
        float sum = 0.0f;
        const auto& row = spread.factor[(size_t) b];
        for (int j = 0; j < kFineBands; ++j)
            sum += row[(size_t) j] * masker[(size_t) j];
        // The masker summed over many narrow bands is a density per coarse band; bring the
        // reference's narrow-band power to the same footing.
        const float refDensity = ref - spread.widthDb[(size_t) b];
        need[(size_t) b] = std::clamp (powerToDb (sum) + s.sensitivityDb - refDensity, 0.0f, s.maxNeedDb);
    }
    return need;
}

Budget splitBudget (const BandArray& needDb, const BudgetSettings& s) noexcept
{
    const auto& layout = BandLayout::get();
    const float amount = std::clamp (s.amount, 0.0f, 2.0f);
    const float spaceAmount = std::clamp (s.spaceAmount, 0.0f, 2.0f);

    Budget out;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto i = (size_t) b;
        const float total = std::max (0.0f, needDb[i]) * amount;
        const bool bassMono = layout.centreHz[i] < s.bassMonoHz;

        const float space = bassMono ? 0.0f
                                     : std::min ({ s.autoSpaceShare * total * spaceAmount, s.monoSafetyDb, total });
        const float eq = s.link ? std::min (total - space, s.eqDepthDb)
                                : std::min ((1.0f - s.autoSpaceShare) * total, s.eqDepthDb);

        out.total[i] = total;
        out.space[i] = space;
        out.eq[i]    = std::max (0.0f, eq);
        out.rest[i]  = std::max (0.0f, total - space - out.eq[i]);
    }
    return out;
}

void BandSmoother::setSpeed (float newSpeed) noexcept
{
    speed = std::clamp (newSpeed, 0.0f, 1.0f);
    updateCoefficients();
}

void BandSmoother::setManualTimes (float attackMs, float releaseMs) noexcept
{
    if (std::abs (attackMs - manualAttack) < 1.0e-4f && std::abs (releaseMs - manualRelease) < 1.0e-4f)
        return;
    manualAttack = attackMs;
    manualRelease = releaseMs;
    updateCoefficients();
}

void BandSmoother::updateCoefficients() noexcept
{
    // Log-interpolate between Rider (slow) and Duck (fast) time constants.
    auto lerpLog = [this] (float slow, float fast) { return slow * std::pow (fast / slow, speed); };
    attack  = manualAttack > 0.0f ? std::clamp (manualAttack, 1.0f, 2000.0f) : lerpLog (180.0f, 8.0f);
    release = manualRelease > 0.0f ? std::clamp (manualRelease, 5.0f, 10000.0f) : lerpLog (1500.0f, 90.0f);

    auto coeff = [this] (float ms) { return (float) std::exp (-1000.0 / (ms * frameRate)); };
    attackCoeff  = coeff (attack);
    releaseCoeff = coeff (release);
}

const BandArray& BandSmoother::process (const BandArray& target) noexcept
{
    for (size_t i = 0; i < state.size(); ++i)
    {
        const float c = target[i] > state[i] ? attackCoeff : releaseCoeff;
        state[i] = target[i] + c * (state[i] - target[i]);
    }
    return state;
}

} // namespace ff
