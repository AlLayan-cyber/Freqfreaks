// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ff/Bands.h"

#include <array>

namespace ff
{

constexpr int kNumBells = 12;
using BellArray = std::array<float, kNumBells>;

/** A fixed bank of broad, overlapping bell filters, and a fitter that turns a
    per-band cut curve into bell gains.

    This is the "parametric approximation" from the spec: the analysis works on
    32 bands, but audio is shaped by a few smooth bells. Bells stay at fixed
    frequencies, so nothing jumps around; only their gains move, and the fit is
    warm-started from the previous frame, which keeps it stable in time. */
class BellBank
{
public:
    void prepare (double sampleRate);

    double sampleRate() const noexcept               { return rate; }
    float centreHz (int bell) const noexcept         { return fc[(size_t) bell]; }
    float q (int bell) const noexcept                { return qs[(size_t) bell]; }

    /** Fits non-negative cuts (dB) so the bank's response approximates cutDb.
        Bells with allowed[k] == false stay at 0. gains is used as warm start and
        receives the result. */
    void fit (const BandArray& cutDb, const std::array<bool, kNumBells>& allowed,
              float maxCutDb, BellArray& gains) const noexcept;

    /** Predicted cut (positive dB) in each analysis band for the given bell cuts. */
    BandArray response (const BellArray& gains) const noexcept;

private:
    double rate = 48000.0;
    BellArray fc {}, qs {};
    std::array<std::array<float, kNumBells>, kNumBands> shape {};   // cut in band b per dB of bell k
    float stepSize = 0.1f;
};

} // namespace ff
