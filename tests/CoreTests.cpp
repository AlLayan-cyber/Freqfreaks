// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the JUCE-free DSP core and the shared bus.

#include "ff/Analyzer.h"
#include "ff/BellBank.h"
#include "ff/SharedBus.h"
#include "ff/StereoProcessor.h"
#include "ff/TrackEngine.h"
#include "ff/Unmasking.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if ! defined (_WIN32)
 #include <sys/mman.h>
 #include <unistd.h>
#endif

namespace
{
int failures = 0;
int checks = 0;

#define CHECK(cond) \
    do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_NEAR(a, b, tol) \
    do { ++checks; const double va_ = (a), vb_ = (b); \
         if (std::abs (va_ - vb_) > (tol)) { ++failures; \
             std::printf ("  FAIL %s:%d: %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #a, va_, vb_, (double) (tol)); } } while (0)

constexpr double kRate = 48000.0;
constexpr double kPi = 3.141592653589793;

std::vector<float> sine (double hz, float amp, int n)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = amp * (float) std::sin (2.0 * kPi * hz * i / kRate);
    return v;
}

std::vector<float> noise (int n, unsigned seed, float amp = 0.25f)
{
    std::mt19937 gen (seed);
    std::normal_distribution<float> dist (0.0f, amp);
    std::vector<float> v ((size_t) n);
    for (auto& x : v)
        x = dist (gen);
    return v;
}

double power (const std::vector<float>& v, int from = 0)
{
    double p = 0.0;
    for (size_t i = (size_t) from; i < v.size(); ++i)
        p += (double) v[i] * v[i];
    return p / (double) (v.size() - (size_t) from);
}

/** Power of x in a narrow frequency region, via a cascade of band-passes. */
double bandPower (const std::vector<float>& x, double hz, double q, int from)
{
    auto c = ff::BiquadCoeffs::bandpass (kRate, hz, q);
    ff::BiquadState s1, s2;
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = s2.process (s1.process (x[i], c), c);
    return power (y, from);
}

//==============================================================================
void testBands()
{
    const auto& l = ff::BandLayout::get();
    for (int b = 1; b < ff::kNumBands; ++b)
        CHECK (l.centreHz[(size_t) b] > l.centreHz[(size_t) b - 1]);
    CHECK_NEAR (l.centreHz[0], ff::kLowestBandHz, 0.5);
    CHECK_NEAR (l.centreHz[ff::kNumBands - 1], ff::kHighestBandHz, 5.0);
    for (int b = 0; b < ff::kNumBands; ++b)
        CHECK (l.edgeHz[(size_t) b] < l.centreHz[(size_t) b] && l.centreHz[(size_t) b] < l.edgeHz[(size_t) b + 1]);
    CHECK (l.bandForHz (1000.0f) >= 0);
}

void testAnalyzer()
{
    ff::Analyzer a;
    a.prepare (kRate);
    const int n = 48000;
    auto mid = sine (1000.0, 0.5f, n);

    a.process (mid.data(), mid.data(), n);   // L = R -> pure Mid
    const int band = ff::BandLayout::get().bandForHz (1000.0f);
    CHECK_NEAR (a.midDb()[(size_t) band], 20.0 * std::log10 (0.5) - 3.0103, 1.0);
    CHECK (a.sideDb()[(size_t) band] < -80.0f);
    CHECK (a.midDb()[(size_t) band - 4] < a.midDb()[(size_t) band] - 40.0f);

    // Side-only signal.
    ff::Analyzer b;
    b.prepare (kRate);
    auto l = sine (3000.0, 0.25f, n);
    std::vector<float> r (l.size());
    for (size_t i = 0; i < l.size(); ++i)
        r[i] = -l[i];
    b.process (l.data(), r.data(), n);
    const int band3k = ff::BandLayout::get().bandForHz (3000.0f);
    CHECK_NEAR (b.sideDb()[(size_t) band3k], 20.0 * std::log10 (0.25) - 3.0103, 1.0);
    CHECK (b.midDb()[(size_t) band3k] < -80.0f);
}

void testNeed()
{
    ff::BandArray targetMid {}, targetSide {}, ref {};
    targetMid.fill (-90.0f);
    targetSide.fill (-120.0f);
    ref.fill (-120.0f);

    const int b = ff::BandLayout::get().bandForHz (2500.0f);
    targetMid[(size_t) b] = -10.0f;
    ref[(size_t) b] = -20.0f;

    ff::MaskingSettings s;
    auto need = ff::computeNeed (targetMid, targetSide, ref, s);
    CHECK_NEAR (need[(size_t) b], 16.0, 0.5);              // -10 + 6 - (-20)
    CHECK (need[(size_t) b - 6] == 0.0f);                  // reference absent there

    // Same masker in the Side masks a centred reference less.
    ff::BandArray sideMid = targetMid, sideSide = targetSide;
    sideMid[(size_t) b] = -120.0f;
    sideSide[(size_t) b] = -10.0f;
    auto needSide = ff::computeNeed (sideMid, sideSide, ref, s);
    CHECK_NEAR (needSide[(size_t) b], 10.0, 0.5);

    // A reference already well above the masker asks for nothing.
    ref[(size_t) b] = 5.0f;
    need = ff::computeNeed (targetMid, targetSide, ref, s);
    CHECK (need[(size_t) b] == 0.0f);
}

void testBudget()
{
    // The worked example from the spec: 4 dB needed at 2 kHz, Link on.
    ff::BandArray need {};
    const int b = ff::BandLayout::get().bandForHz (2000.0f);
    need[(size_t) b] = 4.0f;

    ff::BudgetSettings s;
    s.amount = 1.0f;

    auto run = [&] (float spaceAmount) { s.spaceAmount = spaceAmount; return ff::splitBudget (need, s); };

    auto r100 = run (1.0f);
    CHECK_NEAR (r100.space[(size_t) b], 3.0, 1e-4);
    CHECK_NEAR (r100.eq[(size_t) b], 1.0, 1e-4);
    auto r50 = run (0.5f);
    CHECK_NEAR (r50.space[(size_t) b], 1.5, 1e-4);
    CHECK_NEAR (r50.eq[(size_t) b], 2.5, 1e-4);
    auto r0 = run (0.0f);
    CHECK_NEAR (r0.space[(size_t) b], 0.0, 1e-4);
    CHECK_NEAR (r0.eq[(size_t) b], 4.0, 1e-4);
    auto r133 = run (4.0f / 3.0f);
    CHECK_NEAR (r133.space[(size_t) b], 4.0, 1e-3);
    CHECK_NEAR (r133.eq[(size_t) b], 0.0, 1e-3);

    // Bass mono: Space never works below the threshold.
    ff::BandArray bassNeed {};
    bassNeed[0] = 5.0f;
    s.spaceAmount = 1.0f;
    auto bass = ff::splitBudget (bassNeed, s);
    CHECK (bass.space[0] == 0.0f);
    CHECK_NEAR (bass.eq[0], 5.0, 1e-4);

    // Limits reached -> unresolved remainder.
    need[(size_t) b] = 20.0f;
    s.eqDepthDb = 6.0f;
    s.monoSafetyDb = 6.0f;
    auto capped = ff::splitBudget (need, s);
    CHECK_NEAR (capped.rest[(size_t) b], 8.0, 1e-3);

    // Link off: EQ ignores what Space does.
    need[(size_t) b] = 4.0f;
    s.link = false;
    s.spaceAmount = 0.0f;
    auto unlinked = ff::splitBudget (need, s);
    CHECK_NEAR (unlinked.eq[(size_t) b], 1.0, 1e-4);
    CHECK_NEAR (unlinked.rest[(size_t) b], 3.0, 1e-4);
}

void testSmoother()
{
    ff::BandSmoother sm;
    sm.prepare (93.75);
    sm.setSpeed (0.0f);
    CHECK (sm.attackMs() < sm.releaseMs());
    ff::BandArray target {};
    target.fill (6.0f);
    for (int i = 0; i < 10; ++i)
        sm.process (target);
    const float risen = sm.value()[0];
    CHECK (risen > 1.0f && risen < 6.0f);

    sm.setSpeed (1.0f);
    for (int i = 0; i < 10; ++i)
        sm.process (target);
    CHECK (sm.value()[0] > 5.9f);   // Duck reaches the target in ~100 ms
}

void testBellFit()
{
    ff::BellBank bank;
    bank.prepare (kRate);
    const auto& layout = ff::BandLayout::get();

    ff::BandArray cut {};
    for (int b = 0; b < ff::kNumBands; ++b)
    {
        const double e = ff::hzToErbRate (layout.centreHz[(size_t) b]) - ff::hzToErbRate (2500.0f);
        cut[(size_t) b] = (float) (4.0 * std::exp (-0.5 * e * e / 9.0));
    }

    std::array<bool, ff::kNumBells> allowed {};
    allowed.fill (true);
    ff::BandArray ones {};
    ones.fill (1.0f);
    ff::BellArray gains {};
    for (int i = 0; i < 4; ++i)   // warm-started frames converge further
        bank.fit (cut, ones, allowed, 12.0f, gains);
    const auto resp = bank.response (gains);

    double maxErr = 0.0;
    for (int b = 0; b < ff::kNumBands; ++b)
        maxErr = std::max (maxErr, (double) std::abs (resp[(size_t) b] - cut[(size_t) b]));
    CHECK (maxErr < 1.0);

    // The exact response used for drawing matches the band-centre model.
    for (int b = 4; b < 28; ++b)
        CHECK_NEAR (bank.cutAt (layout.centreHz[(size_t) b], gains), resp[(size_t) b], 0.05);

    allowed.fill (false);
    allowed[8] = true;
    ff::BellArray masked {};
    bank.fit (cut, ones, allowed, 12.0f, masked);
    for (int k = 0; k < ff::kNumBells; ++k)
        if (k != 8)
            CHECK (masked[(size_t) k] == 0.0f);
    CHECK (masked[8] > 0.5f);
}

void testRangeLimits()
{
    // The reported bug: Range from 140 Hz still cut the bass.
    ff::BellBank bank;
    bank.prepare (kRate);
    const auto& layout = ff::BandLayout::get();

    ff::BandArray cut {};
    for (int b = 0; b < ff::kNumBands; ++b)
    {
        const float hz = layout.centreHz[(size_t) b];
        cut[(size_t) b] = (hz >= 140.0f && hz <= 600.0f) ? 4.0f : 0.0f;
    }

    const auto weights = ff::BellBank::rangeWeights (140.0f, 14000.0f);
    const auto allowed = bank.bellsWithin (140.0f * 1.1f, 14000.0f);
    ff::BellArray gains {};
    for (int frame = 0; frame < 20; ++frame)   // about 0.2 s of analysis frames
        bank.fit (cut, weights, allowed, 6.0f, gains);

    for (float hz : { 30.0f, 50.0f, 70.0f, 90.0f, 112.0f })
    {
        const float leak = bank.cutAt (hz, gains);
        std::printf ("  info: Range from 140 Hz, cut at %3.0f Hz = %.2f dB\n", hz, leak);
        CHECK (leak < (hz <= 100.0f ? 0.5f : 1.0f));
    }
    CHECK (bank.cutAt (250.0f, gains) > 3.0f);   // the conflict itself is still released
    CHECK (bank.cutAt (400.0f, gains) > 3.0f);

    // Engine level: Space never works below Bass Mono, EQ never below Range.
    ff::TrackEngine engine;
    engine.prepare (kRate, 512);
    ff::ReferenceSnapshot ref;
    ref.valid = true;
    for (int b = 0; b < ff::kNumBands; ++b)
        ref.midDb[(size_t) b] = -40.0f;   // a broadband reference: every band asks for room
    ff::TrackParams p;
    p.amount = 1.0f;
    p.speed = 1.0f;
    p.rangeLowHz = 140.0f;
    p.bassMonoHz = 200.0f;
    for (int i = 0; i < 300; ++i)
    {
        auto l = noise (512, 400 + (unsigned) i, 0.3f), r = l;
        engine.process (l.data(), r.data(), 512, p, ref);
    }
    const auto& sb = engine.processor().bank();
    float eqBelow = 0.0f, spaceBelow = 0.0f;
    for (float hz : { 40.0f, 60.0f, 80.0f, 98.0f })   // up to 0.7 x Range low
        eqBelow = std::max (eqBelow, sb.cutAt (hz, engine.eqBells()));
    for (float hz : { 40.0f, 80.0f, 120.0f, 140.0f })   // up to 0.7 x Bass Mono
        spaceBelow = std::max (spaceBelow, sb.cutAt (hz, engine.spaceBells()));
    std::printf ("  info: engine, worst EQ cut up to 98 Hz %.2f dB, worst Space up to 140 Hz %.2f dB\n", eqBelow, spaceBelow);
    CHECK (eqBelow < 0.5f);
    CHECK (spaceBelow < 0.5f);
    CHECK (sb.cutAt (1000.0f, engine.eqBells()) + sb.cutAt (1000.0f, engine.spaceBells()) > 3.0f);
}

/** Runs a broadband track against a mid-range reference; returns the output/input energy in dB. */
double runCompensation (const ff::TrackParams& p, float* compDb = nullptr)
{
    ff::TrackEngine e;
    e.prepare (kRate, 512);
    ff::ReferenceSnapshot ref;
    ref.valid = true;
    const auto& layout = ff::BandLayout::get();
    for (int b = 0; b < ff::kNumBands; ++b)
    {
        const float hz = layout.centreHz[(size_t) b];
        ref.midDb[(size_t) b] = (hz > 300.0f && hz < 5000.0f) ? -36.0f : -110.0f;
    }

    const int n = 512 * 560;
    const auto src = noise (n, 77, 0.2f);
    auto l = src, r = src;
    for (int i = 0; i < n; i += 512)
        e.process (l.data() + i, r.data() + i, 512, p, ref);

    const int lat = e.latencySamples(), from = n / 2;
    double in = 0.0, out = 0.0;
    for (int i = from; i < n; ++i)
    {
        in += 2.0 * (double) src[(size_t) (i - lat)] * src[(size_t) (i - lat)];
        out += (double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i];
    }
    if (compDb != nullptr)
        *compDb = e.compensationDb();
    return 10.0 * std::log10 (out / in);
}

void testLoudness()
{
    ff::TrackParams p;
    p.amount = 0.7f;
    p.speed = 1.0f;

    // Manual output trim is exact.
    p.spaceComp = false;
    const double base = runCompensation (p);
    p.outputDb = 6.0f;
    CHECK_NEAR (runCompensation (p) - base, 6.0, 0.05);
    p.outputDb = 0.0f;

    // Space compensation: turning Space up never makes the track louder.
    p.spaceComp = true;
    for (bool link : { true, false })
    {
        p.link = link;
        float comp0 = 0.0f, comp100 = 0.0f;
        p.spaceAmount = 0.0f;
        const double at0 = runCompensation (p, &comp0);
        p.spaceAmount = 1.0f;
        const double at100 = runCompensation (p, &comp100);
        p.spaceAmount = 2.0f;
        const double at200 = runCompensation (p);
        std::printf ("  info: Space comp, Link %s: energy %.2f / %.2f / %.2f dB at 0/100/200 %%, comp %.2f -> %.2f dB\n",
                     link ? "on " : "off", at0, at100, at200, comp0, comp100);
        CHECK_NEAR (comp0, 0.0, 0.05);           // nothing to compensate at 0 %
        CHECK (at100 <= at0 + 0.05);             // never louder...
        CHECK (at200 <= at0 + 0.05);
        CHECK (at100 >= at0 - 0.6);              // ...and only the width allowance quieter
        CHECK (comp100 <= 0.0f);
    }

    // Auto: back to the input's loudness (within the width allowance).
    p.link = true;
    p.autoGain = true;
    for (float space : { 0.0f, 1.0f })
    {
        p.spaceAmount = space;
        float comp = 0.0f;
        const double e = runCompensation (p, &comp);
        std::printf ("  info: Auto at Space %.0f %%: energy %.2f dB (comp +%.2f dB)\n", space * 100.0f, e, comp);
        CHECK (std::abs (e) < 0.4);
        CHECK (comp >= 0.0f && comp <= 6.0f);
    }
}

/** Click detector: a sine under active release, one switch in the middle. A click shows as
    a jump of the second difference above both steady states (before and after). */
struct ClickRun
{
    std::vector<float> l, r;
};

ClickRun runClick (const std::function<void (ff::TrackParams&, ff::ReferenceSnapshot&)>& change, bool fromStart)
{
    constexpr int block = 256;
    ff::TrackEngine e;
    e.prepare (kRate, block);
    ff::ReferenceSnapshot ref;
    ref.valid = true;
    const auto& layout = ff::BandLayout::get();
    for (int b = 0; b < ff::kNumBands; ++b)
    {
        const float hz = layout.centreHz[(size_t) b];
        ref.midDb[(size_t) b] = (hz > 300.0f && hz < 4000.0f) ? -40.0f : -110.0f;
    }

    ff::TrackParams p;
    p.amount = 0.9f;
    p.speed = 0.5f;
    if (fromStart && change)
        change (p, ref);

    const int n = block * 560;
    ClickRun out { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    for (int i = 0; i < n; ++i)
    {
        const float s = 0.3f * (float) std::sin (2.0 * kPi * 1000.0 * i / kRate);
        const float s2 = 0.08f * (float) std::sin (2.0 * kPi * 1500.0 * i / kRate + 1.0);
        out.l[(size_t) i] = s + s2;
        out.r[(size_t) i] = s - 0.5f * s2;
    }
    for (int i = 0; i < n; i += block)
    {
        if (! fromStart && i == block * 400 && change)
            change (p, ref);
        e.process (out.l.data() + i, out.r.data() + i, block, p, ref);
    }
    return out;
}

double peakSecondDifference (const ClickRun& run, int from, int to)
{
    double m = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double a = run.l[(size_t) i] - 2.0 * run.l[(size_t) i - 1] + run.l[(size_t) i - 2];
        const double b = run.r[(size_t) i] - 2.0 * run.r[(size_t) i - 1] + run.r[(size_t) i - 2];
        m = std::max ({ m, std::abs (a), std::abs (b) });
    }
    return m;
}

void testClicks()
{
    using Change = std::function<void (ff::TrackParams&, ff::ReferenceSnapshot&)>;
    const std::pair<const char*, Change> cases[] = {
        { "Space mode -> Pan Away",    [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.spaceMode = ff::SpaceMode::panAway; } },
        { "Space mode -> Decorrelate", [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.spaceMode = ff::SpaceMode::decorrelate; } },
        { "Pan Away side flip",        [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.spaceMode = ff::SpaceMode::panAway; p.panSign = -p.panSign; } },
        { "bypass on",                 [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.bypass = true; } },
        { "Amount 90 -> 0 %",          [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.amount = 0.0f; } },
        { "Space Amount 100 -> 0 %",   [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.spaceAmount = 0.0f; } },
        { "Hub trim to 0",             [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.hubAmount = 0.0f; } },
        { "Bass Mono 150 -> 1200 Hz",  [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.bassMonoHz = 1200.0f; } },
        { "Link off",                  [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.link = false; } },
        { "Auto gain on",              [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.autoGain = true; } },
        { "Output +6 dB",              [] (ff::TrackParams& p, ff::ReferenceSnapshot&) { p.outputDb = 6.0f; } },
        { "reference lost",            [] (ff::TrackParams&, ff::ReferenceSnapshot& r) { r.valid = false; } },
    };

    const int from = 256 * 400, to = from + (int) (0.09 * kRate);
    const double before = peakSecondDifference (runClick (nullptr, false), from, to);
    for (const auto& [name, change] : cases)
    {
        const double peak = peakSecondDifference (runClick (change, false), from, to);
        const double after = peakSecondDifference (runClick (change, true), from, to);
        const double ratio = peak / std::max (before, after);
        if (ratio > 1.5)
            std::printf ("  click: %s (x%.2f)\n", name, ratio);
        CHECK (ratio < 1.5);
    }
}

void runProcessor (ff::StereoProcessor& p, std::vector<float>& l, std::vector<float>& r)
{
    for (size_t pos = 0; pos < l.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, l.size() - pos);
        p.process (l.data() + pos, r.data() + pos, n);
    }
}

void testStereoProcessor()
{
    const int n = 96000;
    const int settle = 24000;

    // Neutral settings pass audio through untouched.
    {
        ff::StereoProcessor p;
        p.prepare (kRate);
        auto l = noise (n, 1), r = noise (n, 2);
        auto l0 = l, r0 = r;
        runProcessor (p, l, r);
        double err = 0.0;
        for (int i = 0; i < n; ++i)
            err = std::max (err, (double) std::abs (l[(size_t) i] - l0[(size_t) i]) + std::abs (r[(size_t) i] - r0[(size_t) i]));
        CHECK (err < 1.0e-4);
    }

    // Spread on a mono source: Mid goes down at 2-3 kHz, Side comes up, total energy roughly kept.
    {
        ff::StereoProcessor p;
        p.prepare (kRate);
        ff::BellArray eq {}, space {};
        space[7] = 6.0f;
        space[8] = 6.0f;
        ff::BellArray tw {};
        for (int i = 0; i < 10; ++i)
            p.bank().fitTransfer (space, tw);
        std::printf ("  info: transfer energy error %.2f dB\n", p.bank().transferErrorDb (space, tw));
        CHECK (p.bank().transferErrorDb (space, tw) < 0.3f);
        p.setTargets (eq, space, tw, ff::SpaceMode::spread, 1.0f);

        auto mono = noise (n, 3);
        auto l = mono, r = mono;
        runProcessor (p, l, r);

        std::vector<float> m ((size_t) n), s ((size_t) n), m0 ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            m[(size_t) i] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
            s[(size_t) i] = 0.5f * (l[(size_t) i] - r[(size_t) i]);
            m0[(size_t) i] = mono[(size_t) i];
        }

        const double hz = p.bank().centreHz (7) * 0.5 + p.bank().centreHz (8) * 0.5;
        const double midDrop = 10.0 * std::log10 (bandPower (m, hz, 4.0, settle) / bandPower (m0, hz, 4.0, settle));
        const double sideRise = 10.0 * std::log10 (bandPower (s, hz, 4.0, settle) / bandPower (m0, hz, 4.0, settle));
        CHECK (midDrop < -3.0);
        CHECK (sideRise > -6.0);

        // Stereo energy (L^2 + R^2) stays the same: in that band and over the whole signal.
        const double energyIn = 2.0 * bandPower (m0, hz, 4.0, settle);
        const double energyOut = bandPower (l, hz, 4.0, settle) + bandPower (r, hz, 4.0, settle);
        CHECK_NEAR (10.0 * std::log10 (energyOut / energyIn), 0.0, 0.3);
        const double totalOut = power (l, settle) + power (r, settle);
        const double totalIn = 2.0 * power (m0, settle);
        std::printf ("  info: Spread, total energy change %.2f dB, band %.2f dB\n",
                     10.0 * std::log10 (totalOut / totalIn), 10.0 * std::log10 (energyOut / energyIn));
        CHECK_NEAR (10.0 * std::log10 (totalOut / totalIn), 0.0, 0.15);

        // Below the Space bells nothing changes.
        const double low = 10.0 * std::log10 (bandPower (m, 120.0, 4.0, settle) / bandPower (m0, 120.0, 4.0, settle));
        CHECK_NEAR (low, 0.0, 0.5);
    }

    // Pan Away moves the band to one side.
    {
        ff::StereoProcessor p;
        p.prepare (kRate);
        ff::BellArray eq {}, space {};
        space[8] = 8.0f;
        ff::BellArray tw {};
        for (int i = 0; i < 10; ++i)
            p.bank().fitTransfer (space, tw);
        p.setTargets (eq, space, tw, ff::SpaceMode::panAway, -1.0f);   // to the right
        auto mono = noise (n, 4);
        auto l = mono, r = mono;
        runProcessor (p, l, r);
        const double hz = p.bank().centreHz (8);
        CHECK (bandPower (r, hz, 4.0, settle) > 1.5 * bandPower (l, hz, 4.0, settle));
    }

    // EQ cut lowers both channels.
    {
        ff::StereoProcessor p;
        p.prepare (kRate);
        ff::BellArray eq {}, space {};
        eq[8] = 6.0f;
        ff::BellArray none {};
        p.setTargets (eq, space, none, ff::SpaceMode::spread, 1.0f);
        auto mono = noise (n, 5);
        auto l = mono, r = mono;
        runProcessor (p, l, r);
        const double hz = p.bank().centreHz (8);
        const double drop = 10.0 * std::log10 (bandPower (l, hz, 4.0, settle) / bandPower (mono, hz, 4.0, settle));
        CHECK (drop < -4.0 && drop > -8.0);
    }
}

void testTrackEngine()
{
    ff::TrackEngine engine;
    engine.prepare (kRate, 512);
    CHECK (engine.latencySamples() == (int) std::lround (0.030 * kRate));

    // Reference: a vocal-like band around 1-4 kHz.
    ff::ReferenceSnapshot ref;
    ref.valid = true;
    const auto& layout = ff::BandLayout::get();
    for (int b = 0; b < ff::kNumBands; ++b)
    {
        const float hz = layout.centreHz[(size_t) b];
        ref.midDb[(size_t) b] = (hz > 1000.0f && hz < 4000.0f) ? -33.0f : -110.0f;
    }

    ff::TrackParams params;
    params.amount = 1.0f;
    params.speed = 1.0f;

    const int n = 96000;
    auto mono = noise (n, 6);
    auto l = mono, r = mono;
    for (size_t pos = 0; pos < (size_t) n; pos += 512)
    {
        const int len = (int) std::min<size_t> (512, (size_t) n - pos);
        engine.process (l.data() + pos, r.data() + pos, len, params, ref);
    }

    CHECK (engine.hasReference());
    const int b2k = layout.bandForHz (2000.0f);
    CHECK (engine.budget().total[(size_t) b2k] > 3.0f);
    std::printf ("  info: 2 kHz total %.2f space %.2f eq %.2f rest %.2f\n", engine.budget().total[(size_t) b2k], engine.budget().space[(size_t) b2k], engine.budget().eq[(size_t) b2k], engine.budget().rest[(size_t) b2k]);
    CHECK (engine.budget().space[(size_t) b2k] > engine.budget().eq[(size_t) b2k]);
    CHECK (engine.budget().total[(size_t) layout.bandForHz (150.0f)] < 0.5f);

    // Align for latency, then compare the Mid at 2 kHz before and after.
    const int lat = engine.latencySamples();
    std::vector<float> midOut ((size_t) (n - lat)), midIn ((size_t) (n - lat));
    for (int i = 0; i < n - lat; ++i)
    {
        midOut[(size_t) i] = 0.5f * (l[(size_t) (i + lat)] + r[(size_t) (i + lat)]);
        midIn[(size_t) i] = mono[(size_t) i];
    }
    const double drop = 10.0 * std::log10 (bandPower (midOut, 2000.0, 4.0, 48000) / bandPower (midIn, 2000.0, 4.0, 48000));
    CHECK (drop < -3.0);
    const double lowDrop = 10.0 * std::log10 (bandPower (midOut, 200.0, 4.0, 48000) / bandPower (midIn, 200.0, 4.0, 48000));
    CHECK_NEAR (lowDrop, 0.0, 1.0);

    // Without a reference the track is left alone (after release).
    ff::ReferenceSnapshot none;
    for (int i = 0; i < 400; ++i)
    {
        auto blockL = noise (512, 100 + (unsigned) i), blockR = blockL;
        engine.process (blockL.data(), blockR.data(), 512, params, none);
    }
    CHECK (engine.budget().total[(size_t) b2k] < 0.1f);
}

void testSharedBus()
{
    const std::string region = "/fftest-" + std::to_string (ff::SharedBus::makeInstanceId() % 1000000);

    {
        ff::SharedBus a (region), b (region), hub (region);
        CHECK (a.isShared());

        const auto idA = ff::SharedBus::makeInstanceId();
        const auto idB = ff::SharedBus::makeInstanceId();
        CHECK (a.claim (idA));
        CHECK (b.claim (idB));
        CHECK (a.slotIndex() != b.slotIndex());

        ff::bus::TrackData d;
        d.kind = ff::bus::Kind::track;
        std::snprintf (d.name, sizeof (d.name), "%s", "Vocal");
        d.levelMidDb[5] = -12.5f;
        a.publish (d);

        ff::bus::TrackData out;
        bool stale = true;
        CHECK (b.read (idA, out, &stale));
        CHECK (! stale);
        CHECK_NEAR (out.levelMidDb[5], -12.5, 1e-6);
        CHECK (std::string (out.name) == "Vocal");

        auto entries = hub.list();
        CHECK (entries.size() == 2);

        // A duplicate of a live id is refused.
        ff::SharedBus dup (region);
        CHECK (! dup.claim (idA));

        // Hub block.
        ff::bus::HubData h;
        CHECK (! a.readHub (h));
        CHECK (hub.claimHub (ff::SharedBus::makeInstanceId()));
        h.totalAmount = 0.5f;
        hub.publishHub (h);
        ff::bus::HubData hr;
        CHECK (a.readHub (hr));
        CHECK_NEAR (hr.totalAmount, 0.5, 1e-6);

        // Seqlock: a reader never sees a half-written record.
        std::atomic<bool> stop { false };
        std::thread writer ([&] {
            ff::bus::TrackData w;
            w.kind = ff::bus::Kind::track;
            for (int i = 0; ! stop.load(); ++i)
            {
                for (auto& v : w.levelMidDb) v = (float) i;
                for (auto& v : w.restDb) v = (float) i;
                a.publish (w);
            }
        });
        int torn = 0;
        for (int i = 0; i < 200000; ++i)
        {
            ff::bus::TrackData rd;
            if (b.read (idA, rd))
                if (rd.levelMidDb[0] != rd.restDb[ff::kNumBands - 1] || rd.levelMidDb[3] != rd.levelMidDb[20])
                    ++torn;
        }
        stop = true;
        writer.join();
        CHECK (torn == 0);

        // Hub trims are found by track id.
        ff::bus::HubData withTrims;
        withTrims.trims[3].id = idB;
        withTrims.trims[3].trim = 0.5f;
        withTrims.trims[3].flags = 1u;
        CHECK_NEAR (withTrims.trimFor (idB).trim, 0.5, 1e-6);
        CHECK (withTrims.trimFor (idB).flags == 1u);
        CHECK_NEAR (withTrims.trimFor (idA).trim, 1.0, 1e-6);   // no entry -> neutral

        // Remote commands: the Hub writes, the track reads each new one once.
        ff::bus::RemoteCommand cmd;
        cmd.mask = ff::bus::remoteAmount;
        cmd.amount = 0.25f;
        CHECK (hub.sendRemote (idB, cmd));
        ff::bus::RemoteCommand got;
        CHECK (b.readRemote (got, 0));
        CHECK_NEAR (got.amount, 0.25, 1e-6);
        CHECK (! b.readRemote (got, got.serial));               // already seen
        CHECK (! hub.sendRemote (ff::SharedBus::makeInstanceId(), cmd));   // unknown track

        // A new owner of a slot never sees the previous owner's command.
        const int oldSlot = b.slotIndex();
        b.release();
        ff::SharedBus fresh (region);
        CHECK (fresh.claim (ff::SharedBus::makeInstanceId()));
        if (fresh.slotIndex() == oldSlot)
            CHECK (! fresh.readRemote (got, 0));
        fresh.release();

        CHECK (hub.list().size() == 1);
    }

   #if ! defined (_WIN32)
    shm_unlink (region.c_str());
   #endif
}

} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] = {
        { "bands", testBands },
        { "analyzer", testAnalyzer },
        { "masking need", testNeed },
        { "release budget", testBudget },
        { "smoother", testSmoother },
        { "bell fit", testBellFit },
        { "range limits", testRangeLimits },
        { "stereo processor", testStereoProcessor },
        { "track engine", testTrackEngine },
        { "loudness compensation", testLoudness },
        { "no clicks on switches", testClicks },
        { "shared bus", testSharedBus },
    };

    for (const auto& [name, fn] : tests)
    {
        const int before = failures;
        fn();
        std::printf ("%s %s\n", failures == before ? "[ ok ]" : "[FAIL]", name);
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
