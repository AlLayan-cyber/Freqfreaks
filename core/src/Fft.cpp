// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/Fft.h"

#include <cmath>
#include <utility>

namespace ff
{

void Fft::prepare (int order)
{
    n = 1 << order;
    bitReverse.assign ((size_t) n, 0);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b))
                r |= 1 << (order - 1 - b);
        bitReverse[(size_t) i] = r;
    }

    twiddles.resize ((size_t) n / 2);
    const double twoPi = 6.283185307179586;
    for (int k = 0; k < n / 2; ++k)
        twiddles[(size_t) k] = std::polar (1.0f, (float) (-twoPi * k / n));
}

void Fft::forward (std::complex<float>* data) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const int r = bitReverse[(size_t) i];
        if (r > i)
            std::swap (data[i], data[r]);
    }

    // Butterflies in plain real arithmetic: std::complex multiplication goes through a
    // NaN/Inf-safe library call (__mulsc3) without -ffast-math, several times slower.
    float* d = reinterpret_cast<float*> (data);   // std::complex<float> is layout-compatible with float[2]
    const float* tw = reinterpret_cast<const float*> (twiddles.data());

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        const int stride = n / len;
        for (int start = 0; start < n; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const float wr = tw[2 * k * stride], wi = tw[2 * k * stride + 1];
                float* a = d + 2 * (start + k);
                float* b = d + 2 * (start + k + half);
                const float br = b[0] * wr - b[1] * wi;
                const float bi = b[0] * wi + b[1] * wr;
                b[0] = a[0] - br;
                b[1] = a[1] - bi;
                a[0] += br;
                a[1] += bi;
            }
        }
    }
}

} // namespace ff
