// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <vector>

namespace ff
{

/** Minimal in-place radix-2 complex FFT. Allocates only in prepare(). */
class Fft
{
public:
    void prepare (int order);
    int size() const noexcept { return n; }

    /** Forward transform of data (length size()), in place. */
    void forward (std::complex<float>* data) const noexcept;

private:
    int n = 0;
    std::vector<int> bitReverse;
    std::vector<std::complex<float>> twiddles;
};

} // namespace ff
