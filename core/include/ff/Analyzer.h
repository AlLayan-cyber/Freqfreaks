// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ff/Bands.h"
#include "ff/Fft.h"

#include <complex>
#include <vector>

namespace ff
{

/** Mid/Side band analyser.

    Feeds stereo audio into a sliding Hann-windowed STFT and, every hop, produces
    the power of the Mid and Side signals in each perceptual band (in dB, where
    0 dB is the power of a full-scale DC-free signal, i.e. a full-scale sine reads
    about -3 dB). Mid and Side are transformed together with one complex FFT.

    Real-time safe after prepare(). */
class Analyzer
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** Pushes samples; returns how many new analysis frames were produced. right may equal left for mono. */
    int process (const float* left, const float* right, int numSamples) noexcept;

    const BandArray& midDb() const noexcept   { return mid; }
    const BandArray& sideDb() const noexcept  { return side; }

    /** The same spectrum at fine resolution (narrow Sharp mode). */
    const FineArray& fineMidDb() const noexcept   { return fineMid; }
    const FineArray& fineSideDb() const noexcept  { return fineSide; }

    int hopSize() const noexcept              { return hop; }
    double frameRateHz() const noexcept       { return rate / (double) hop; }

private:
    void analyseFrame() noexcept;

    double rate = 48000.0;
    int fftSize = 2048, hop = 512, writePos = 0, samplesSinceFrame = 0;
    Fft fft;
    std::vector<float> window, ringMid, ringSide;
    std::vector<std::complex<float>> scratch;
    std::vector<int> bandFirstBin, bandLastBin, fineFirstBin, fineLastBin;
    std::vector<float> binMid, binSide;   // per-bin power of the last frame
    float norm = 1.0f;
    BandArray mid {}, side {};
    FineArray fineMid {}, fineSide {};
};

} // namespace ff
