// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/Analyzer.h"

#include <algorithm>
#include <cmath>

namespace ff
{

void Analyzer::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    // ~43 ms window: 2048 at 44.1/48 kHz, 4096 at 88.2/96 kHz, and so on.
    int order = 11;
    while ((1 << order) < (int) (rate * 0.040) && order < 15)
        ++order;
    while ((1 << order) > (int) (rate * 0.090) && order > 9)
        --order;

    fftSize = 1 << order;
    hop = fftSize / 4;
    fft.prepare (order);

    window.resize ((size_t) fftSize);
    double sumSq = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (6.283185307179586 * i / fftSize);
        window[(size_t) i] = (float) w;
        sumSq += w * w;
    }
    norm = (float) (fftSize * sumSq * 0.5);

    ringMid.assign ((size_t) fftSize, 0.0f);
    ringSide.assign ((size_t) fftSize, 0.0f);
    scratch.assign ((size_t) fftSize, {});

    const auto& layout = BandLayout::get();
    const double binHz = rate / fftSize;
    const int maxBin = fftSize / 2 - 1;
    bandFirstBin.assign (kNumBands, 0);
    bandLastBin.assign (kNumBands, 0);

    for (int b = 0; b < kNumBands; ++b)
    {
        int first = (int) std::ceil (layout.edgeHz[(size_t) b] / binHz);
        int last  = (int) std::ceil (layout.edgeHz[(size_t) b + 1] / binHz) - 1;
        first = std::clamp (first, 1, maxBin);
        last  = std::clamp (last, 1, maxBin);

        if (last < first)   // band narrower than one bin: use the bin nearest its centre
            first = last = std::clamp ((int) std::lround (layout.centreHz[(size_t) b] / binHz), 1, maxBin);

        bandFirstBin[(size_t) b] = first;
        bandLastBin[(size_t) b] = last;
    }

    const auto& fine = FineLayout::get();
    fineFirstBin.assign (kFineBands, 0);
    fineLastBin.assign (kFineBands, 0);
    for (int b = 0; b < kFineBands; ++b)
    {
        int first = std::clamp ((int) std::ceil (fine.edgeHz[(size_t) b] / binHz), 1, maxBin);
        int last  = std::clamp ((int) std::ceil (fine.edgeHz[(size_t) b + 1] / binHz) - 1, 1, maxBin);
        if (last < first)
            first = last = std::clamp ((int) std::lround (fine.centreHz[(size_t) b] / binHz), 1, maxBin);
        fineFirstBin[(size_t) b] = first;
        fineLastBin[(size_t) b] = last;
    }
    binMid.assign ((size_t) fftSize / 2, 0.0f);
    binSide.assign ((size_t) fftSize / 2, 0.0f);

    reset();
}

void Analyzer::reset() noexcept
{
    std::fill (ringMid.begin(), ringMid.end(), 0.0f);
    std::fill (ringSide.begin(), ringSide.end(), 0.0f);
    writePos = 0;
    samplesSinceFrame = 0;
    mid.fill (kSilenceDb);
    side.fill (kSilenceDb);
    fineMid.fill (kSilenceDb);
    fineSide.fill (kSilenceDb);
}

int Analyzer::process (const float* left, const float* right, int numSamples) noexcept
{
    int frames = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i];
        const float r = right != nullptr ? right[i] : l;
        ringMid[(size_t) writePos]  = 0.5f * (l + r);
        ringSide[(size_t) writePos] = 0.5f * (l - r);
        writePos = (writePos + 1) & (fftSize - 1);

        if (++samplesSinceFrame >= hop)
        {
            samplesSinceFrame = 0;
            analyseFrame();
            ++frames;
        }
    }
    return frames;
}

void Analyzer::analyseFrame() noexcept
{
    // Oldest sample sits at writePos. Pack Mid into real, Side into imaginary part.
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (writePos + i) & (fftSize - 1);
        const float w = window[(size_t) i];
        scratch[(size_t) i] = { ringMid[(size_t) idx] * w, ringSide[(size_t) idx] * w };
    }

    fft.forward (scratch.data());

    // Per-bin Mid and Side power (the same arithmetic as before, kept per bin so the
    // coarse and the fine bands are summed from it).
    const int maxBin = fftSize / 2 - 1;
    for (int k = 1; k <= maxBin; ++k)
    {
        const auto z  = scratch[(size_t) k];
        const auto zc = std::conj (scratch[(size_t) (fftSize - k)]);
        const auto xm = 0.5f * (z + zc);                                   // spectrum of Mid
        const auto xs = std::complex<float> (0.0f, -0.5f) * (z - zc);      // spectrum of Side
        binMid[(size_t) k] = std::norm (xm);
        binSide[(size_t) k] = std::norm (xs);
    }

    for (int b = 0; b < kNumBands; ++b)
    {
        double pm = 0.0, ps = 0.0;
        for (int k = bandFirstBin[(size_t) b]; k <= bandLastBin[(size_t) b]; ++k)
        {
            pm += (double) binMid[(size_t) k];
            ps += (double) binSide[(size_t) k];
        }
        mid[(size_t) b]  = powerToDb ((float) (pm / norm));
        side[(size_t) b] = powerToDb ((float) (ps / norm));
    }

    for (int b = 0; b < kFineBands; ++b)
    {
        double pm = 0.0, ps = 0.0;
        for (int k = fineFirstBin[(size_t) b]; k <= fineLastBin[(size_t) b]; ++k)
        {
            pm += (double) binMid[(size_t) k];
            ps += (double) binSide[(size_t) k];
        }
        fineMid[(size_t) b]  = powerToDb ((float) (pm / norm));
        fineSide[(size_t) b] = powerToDb ((float) (ps / norm));
    }
}

} // namespace ff
