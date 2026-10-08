// OPL2 FM music test (port round 6, task A), no SDL:
//  1. the software YM3812 (mcport/opl_chip) by construction: the frequency of a single sine operator for
//     several F-number / block / MULT settings (+-0.5 %), envelope timings for AR / DR / RR against the
//     data sheet's tables (+-10 %) and the key-scale rate, TL and KSL attenuation steps, the four waveforms
//     (and WSE off), operator feedback (harmonic content), tremolo depth, vibrato, rhythm mode (each of
//     the five instruments sounds on its own operator only, nothing without the rhythm bit), timers;
//  2. the instrument banks data/inst.bnk and data/drum.bnk: header, 128 records each, names, every
//     parameter in range, the driver's in-place packing (record 0 by hand, the last two left unpacked),
//     the drum pitch bytes;
//  3. the HMI OPL driver (mcport/opl_driver, a translation of snd_opl_* in carpet.exe): a scripted MIDI
//     sequence against the exact register writes worked out by hand from the disassembly (init, note on
//     / off, velocity, controller 7, pitch bend up / down, drum note, all notes off, reset) and the voice
//     allocation / stealing;
//  4. every song of the FM banks music0-0 / music1-0 played through MusicSequencer -> OplMidiOut
//     (AudioEngine, CLOCK_RENDER, 44100 Hz): statistics (programs, note ranges, controllers, voices in
//     use, level) and, with argv[2], the first 60 s of each written to <argv[2]>/music<set>-0-<n>.wav;
//  5. the game side: sound.cpp's music_load_bank with g_music_device 0 loads music0-0 and
//     music_play_track / music_stop drive the OPL output (the stop ends with the driver reset).
// argv[1] = game dir, argv[2] = output directory for the .wav files (optional).
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "opl_chip.h"
#include "opl_driver.h"
#include "audio_mixer.h"
#include "sound.h"
#include "sndbank.h"
#include "hmp.h"
#include "mcfile.h"
#include "gen/opl_tables.h"
#include "crash_handler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { \
    std::printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); g_fail++; } } while (0)
#define CHECK_NEAR(a, b, rel) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (rel) * std::fabs(_b))) { \
    std::printf("FAIL %s:%d: %s ~ %s (%.4f vs %.4f, tol %.1f %%)\n", __FILE__, __LINE__, #a, #b, _a, _b, (rel) * 100.0); g_fail++; } } while (0)

static std::string g_out;
static const double RATE = OplChip::RATE;

// ---- helpers --------------------------------------------------------------------------------------------
static std::vector<int32_t> run(OplChip &c, int n) {
    std::vector<int32_t> s((size_t)n);
    for (int i = 0; i < n; i++) s[(size_t)i] = c.sample();
    return s;
}
// Channel 0: modulator (op 0) silent, carrier (op 3) a plain sine: AR 15, DR 0, SL 0, sustained.
static void setup_sine(OplChip &c, int fnum, int block, int mult = 1, int tl = 0, int ksl = 0) {
    c.write(0x01, 0x20);
    c.write(0x20, 0x21); c.write(0x40, 0x3f); c.write(0x60, 0x00); c.write(0x80, 0x00);   // AR 0: never rises
    c.write(0x23, (uint8_t)(0x20 | mult)); c.write(0x43, (uint8_t)((ksl << 6) | tl)); c.write(0x63, 0xf0); c.write(0x83, 0x00);
    c.write(0xc0, 0x01);                                         // additive (the modulator stays silent)
    c.write(0xa0, (uint8_t)(fnum & 0xff));
    c.write(0xb0, (uint8_t)(0x20 | (block << 2) | (fnum >> 8)));
}
static double measure_freq(const std::vector<int32_t> &s) {
    // rising zero crossings with linear interpolation between samples
    double first = -1, last = -1;
    int n = 0;
    for (size_t i = 1; i < s.size(); i++) {
        if (s[i - 1] < 0 && s[i] >= 0) {
            double t = (double)(i - 1) + (double)-s[i - 1] / (double)(s[i] - s[i - 1]);
            if (first < 0) first = t;
            last = t;
            n++;
        }
    }
    return n > 1 ? (n - 1) * RATE / (last - first) : 0.0;
}
static int peak(const std::vector<int32_t> &s, size_t from = 0) {
    int p = 0;
    for (size_t i = from; i < s.size(); i++) p = std::max(p, std::abs(s[i]));
    return p;
}

// ---- 1. the chip ------------------------------------------------------------------------------------------
static void test_frequency() {
    std::printf("== chip: frequency\n");
    struct Case { int fnum, block, mult; } cases[] = {
        {577, 4, 1}, {343, 2, 1}, {1023, 7, 1}, {577, 4, 2}, {577, 4, 0}, {400, 3, 15}, {600, 5, 11}};
    static const int multx2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
    for (const Case &k : cases) {
        OplChip c;
        setup_sine(c, k.fnum, k.block, k.mult);
        auto s = run(c, (int)RATE);
        double want = k.fnum * std::pow(2.0, k.block) * multx2[k.mult] / 2.0 * RATE / 1048576.0;
        double got = measure_freq(s);
        std::printf("  fnum %4d block %d mult %2d: %9.3f Hz (expected %9.3f)\n", k.fnum, k.block, k.mult, got, want);
        CHECK_NEAR(got, want, 0.005);
    }
}

// Data sheet (YM3812 application manual) times in ms for rates 1..15 at Rof 0: attack 0 -> 100 %,
// decay / release 0 dB -> 96 dB.
static const double kAttackMs[16] = {0, 2826.24, 1413.12, 706.56, 353.28, 176.76, 88.32, 44.16, 22.08, 11.04, 5.52, 2.76, 1.40, 0.70, 0.38, 0.0};
static const double kDecayMs[16] = {0, 39280.64, 19640.32, 9820.16, 4910.08, 2455.04, 1227.52, 613.76, 306.88, 153.44, 76.72, 38.36, 19.20, 9.60, 4.80, 2.40};

static long count_until(OplChip &c, int op, bool (*cond)(const OplChip &, int), long limit) {
    long n = 0;
    while (!cond(c, op) && n < limit) { c.sample(); n++; }
    return n;
}

static void test_envelope() {
    std::printf("== chip: envelope\n");
    for (int ar : {1, 4, 8, 12, 13, 14}) {
        OplChip c;
        c.write(0x23, 0x20); c.write(0x43, 0); c.write(0x63, (uint8_t)(ar << 4)); c.write(0x83, 0);
        c.write(0xa0, 0x00); c.write(0xb0, 0x21);                           // block 0, F-number 0x100: Rof 0
        long n = count_until(c, 3, [](const OplChip &x, int op) { return x.env_attenuation(op) == 0; }, 1L << 24);
        double ms = n * 1000.0 / RATE;
        std::printf("  AR %2d: attack %9.2f ms (data sheet %9.2f)\n", ar, ms, kAttackMs[ar]);
        CHECK_NEAR(ms, kAttackMs[ar], ar == 14 ? 0.15 : 0.10);              // AR 14: 17 vs 18.9 samples
    }
    {   // AR 15: immediate
        OplChip c;
        c.write(0x23, 0x20); c.write(0x63, 0xf0); c.write(0xb0, 0x21);
        c.sample();
        CHECK_EQ(c.env_attenuation(3), 0);
    }
    for (int dr : {1, 4, 8, 12, 15}) {                                       // decay 0 -> SL 8 (24 dB)
        OplChip c;
        c.write(0x23, 0x20); c.write(0x63, (uint8_t)(0xf0 | dr)); c.write(0x83, 0x80);
        c.write(0xa0, 0x00); c.write(0xb0, 0x21);
        c.sample();
        long n = count_until(c, 3, [](const OplChip &x, int op) { return x.env_attenuation(op) >= 128; }, 1L << 26);
        double ms = n * 1000.0 / RATE, want = kDecayMs[dr] * 128.0 / 512.0;
        std::printf("  DR %2d: decay to 24 dB %9.2f ms (data sheet %9.2f)\n", dr, ms, want);
        CHECK_NEAR(ms, want, 0.10);
        CHECK(c.env_state(3) == OplChip::ENV_SUSTAIN);
        for (int i = 0; i < 1000; i++) c.sample();
        CHECK_EQ(c.env_attenuation(3), 128);                                 // EGT 1 holds the sustain level
    }
    for (int rr : {2, 6, 10, 14}) {                                          // release 0 -> 96 dB
        OplChip c;
        c.write(0x23, 0x20); c.write(0x63, 0xf0); c.write(0x83, (uint8_t)rr);
        c.write(0xa0, 0x00); c.write(0xb0, 0x21);
        for (int i = 0; i < 10; i++) c.sample();
        c.write(0xb0, 0x01);
        long n = count_until(c, 3, [](const OplChip &x, int op) { return x.env_attenuation(op) >= 511; }, 1L << 26);
        double ms = n * 1000.0 / RATE, want = kDecayMs[rr];
        std::printf("  RR %2d: release %9.2f ms (data sheet %9.2f)\n", rr, ms, want);
        CHECK_NEAR(ms, want, 0.10);
        CHECK(c.env_state(3) == OplChip::ENV_OFF);
    }
    {   // EGT 0 (percussive): after the decay to SL it goes on at RR with the key still on
        OplChip c;
        c.write(0x23, 0x00); c.write(0x63, 0xff); c.write(0x83, 0x2f); c.write(0xb0, 0x21);
        for (int i = 0; i < 2000; i++) c.sample();
        CHECK_EQ(c.env_attenuation(3), 511);
    }
    {   // key-scale rate: block 7 / F-number bit 9 -> key scale number 15; KSR 1 adds 15, KSR 0 adds 3
        long t[2];
        for (int ksr = 0; ksr < 2; ksr++) {
            OplChip c;
            c.write(0x23, (uint8_t)(0x20 | (ksr << 4))); c.write(0x63, 0xf4); c.write(0x83, 0xf0);
            c.write(0xa0, 0x00); c.write(0xb0, (uint8_t)(0x20 | (7 << 2) | 2));
            c.sample();
            t[ksr] = count_until(c, 3, [](const OplChip &x, int op) { return x.env_attenuation(op) >= 256; }, 1L << 26);
        }
        double ratio = (double)t[0] / (double)t[1];
        std::printf("  KSR: decay R 19 / R 31 time ratio %.2f (expected 8)\n", ratio);
        CHECK_NEAR(ratio, 8.0, 0.10);
    }
}

static void test_levels() {
    std::printf("== chip: TL / KSL\n");
    int base = 0;
    for (int tl : {0, 8, 16, 32}) {
        OplChip c;
        setup_sine(c, 577, 4, 1, tl);
        auto s = run(c, 4000);
        int p = peak(s, 100);
        if (tl == 0) base = p;
        double want = base * std::pow(10.0, -0.75 * tl / 20.0);
        std::printf("  TL %2d: peak %5d (expected %7.1f, -%.2f dB)\n", tl, p, want, 0.75 * tl);
        CHECK_NEAR(p, want, 0.04);
        CHECK_EQ(c.total_attenuation(3), tl * 4);
    }
    CHECK(base > 4000 && base < 4200);
    // KSL at block 7 F-number 0x3ff: 21 dB at 3 dB/oct (KSL 1), 10.5 dB (KSL 2), 42 dB (KSL 3)
    const int want_units[4] = {0, 112, 56, 224};
    for (int ksl = 0; ksl < 4; ksl++) {
        OplChip c;
        setup_sine(c, 0x3ff, 7, 1, 0, ksl);
        c.sample();
        std::printf("  KSL %d at block 7 / 0x3ff: %3d units = %.2f dB\n", ksl, c.total_attenuation(3), c.total_attenuation(3) * 0.1875);
        CHECK_EQ(c.total_attenuation(3), want_units[ksl]);
    }
    {   // one octave lower is 3 dB less (KSL 1), F-number 0x100 at block 4: 40 - 24 = 16 base units
        OplChip c;
        setup_sine(c, 0x3ff, 6, 1, 0, 1);
        c.sample();
        CHECK_EQ(c.total_attenuation(3), 112 - 16);
        OplChip d;
        setup_sine(d, 0x100, 4, 1, 0, 1);
        d.sample();
        CHECK_EQ(d.total_attenuation(3), 32);
    }
    {   // amplitude with KSL 1 at block 7 / 0x3ff: -21 dB
        OplChip c;
        setup_sine(c, 0x3ff, 7, 1, 0, 1);
        auto s = run(c, 2000);
        CHECK_NEAR(peak(s, 100), base * std::pow(10.0, -21.0 / 20.0), 0.06);
    }
}

static void test_waveforms() {
    std::printf("== chip: waveforms\n");
    for (int wse = 0; wse < 2; wse++) {
        for (int ws = 0; ws < 4; ws++) {
            OplChip c;
            setup_sine(c, 512, 2);                                         // 97.1 Hz: 512 samples per cycle
            c.write(0x01, (uint8_t)(wse ? 0x20 : 0x00));
            c.write(0xe3, (uint8_t)ws);
            auto s = run(c, 512 * 8);
            int mn = 0, mx = 0, zeros = 0, firstq = 0, thirdq = 0;
            for (size_t i = 512; i < s.size(); i++) {
                mn = std::min(mn, s[i]); mx = std::max(mx, s[i]);
                if (s[i] == 0) zeros++;
                int q = (int)((i % 512) / 128);                            // phase quarter (phase 0 at sample 0)
                if (s[i] != 0 && q == 0) firstq++;
                if (s[i] != 0 && q == 2) thirdq++;
            }
            double zf = zeros / (double)(s.size() - 512);
            int eff = wse ? ws : 0;
            std::printf("  WSE %d ws %d: min %6d max %6d zero %.2f\n", wse, ws, mn, mx, zf);
            CHECK(mx > 4000);
            if (eff == 0) { CHECK(mn < -4000); CHECK(zf < 0.02); }
            if (eff == 1) { CHECK_EQ(mn, 0); CHECK(zf > 0.48 && zf < 0.53); }
            if (eff == 2) { CHECK_EQ(mn, 0); CHECK(zf < 0.02); }
            if (eff == 3) { CHECK_EQ(mn, 0); CHECK(zf > 0.48 && zf < 0.53); CHECK(firstq > 0); CHECK(thirdq > 0); }
        }
    }
}

// Fraction of the signal power at the fundamental (Goertzel over whole cycles).
static double fundamental_fraction(const std::vector<int32_t> &s, double cycles_per_sample) {
    double w = 2 * 3.14159265358979323846 * cycles_per_sample, cr = std::cos(w);
    double s1 = 0, s2 = 0, pw = 0;
    for (int32_t x : s) { double s0 = x + 2 * cr * s1 - s2; s2 = s1; s1 = s0; pw += (double)x * x; }
    double mag2 = s1 * s1 + s2 * s2 - 2 * cr * s1 * s2;
    return (2.0 * mag2 / (double)s.size()) / pw;
}

static void test_feedback() {
    std::printf("== chip: feedback\n");
    double prev = 2.0;
    for (int fb : {0, 2, 4, 5, 6, 7}) {
        OplChip c;
        c.write(0x01, 0x20);
        c.write(0x20, 0x21); c.write(0x40, 0x00); c.write(0x60, 0xf0); c.write(0x80, 0x00);    // modulator sounds
        c.write(0x23, 0x21); c.write(0x43, 0x3f); c.write(0x63, 0x00); c.write(0x83, 0x00);    // carrier silent (AR 0)
        c.write(0xc0, (uint8_t)((fb << 1) | 1));
        c.write(0xa0, 0x00); c.write(0xb0, 0x20 | (2 << 2) | 2);         // F-number 512, block 2: 512 samples / cycle
        run(c, 1024);
        auto s = run(c, 512 * 64);
        double f = fundamental_fraction(s, 1.0 / 512.0);
        std::printf("  FB %d: fundamental %.4f of the power\n", fb, f);
        if (fb == 0) CHECK(f > 0.995);
        if (fb == 2) CHECK(f < prev);                                       // (not monotonic beyond: Bessel-like)
        if (fb == 0) prev = f;
        if (fb >= 4) CHECK(f < 0.8);
    }
    {   // FM: a modulator at TL 0 changes the carrier's spectrum; CON 1 adds the two
        OplChip c;
        c.write(0x01, 0x20);
        c.write(0x20, 0x21); c.write(0x40, 0x10); c.write(0x60, 0xf0);
        c.write(0x23, 0x21); c.write(0x43, 0x00); c.write(0x63, 0xf0);
        c.write(0xc0, 0x00);
        c.write(0xa0, 0x00); c.write(0xb0, 0x20 | (2 << 2) | 2);
        run(c, 1024);
        auto s = run(c, 512 * 64);
        double f = fundamental_fraction(s, 1.0 / 512.0);
        std::printf("  FM (modulator TL 16): fundamental %.4f\n", f);
        CHECK(f < 0.9);
    }
}

static void test_lfo() {
    std::printf("== chip: tremolo / vibrato\n");
    for (int dam = 0; dam < 2; dam++) {
        OplChip c;
        setup_sine(c, 512, 4);
        c.write(0x23, 0xa1);                                               // AM on
        c.write(0xbd, (uint8_t)(dam ? 0x80 : 0x00));
        auto s = run(c, (int)RATE / 2);
        int mx = 0, mn = 1 << 30;
        for (size_t i = 0; i + 128 <= s.size(); i += 128) {
            int p = 0;
            for (size_t k = i; k < i + 128; k++) p = std::max(p, std::abs(s[k]));
            mx = std::max(mx, p); mn = std::min(mn, p);
        }
        double db = 20 * std::log10((double)mx / mn);
        std::printf("  tremolo DAM %d: %.2f dB peak to peak (data sheet %s)\n", dam, db, dam ? "4.8" : "1.0");
        CHECK_NEAR(db, dam ? 4.875 : 1.125, 0.15);
    }
    for (int dvb = 0; dvb < 2; dvb++) {
        OplChip c;
        setup_sine(c, 1023, 5);                                            // 1551 Hz
        c.write(0x23, 0x61);                                               // VIB on
        c.write(0xbd, (uint8_t)(dvb ? 0x40 : 0x00));
        auto s = run(c, 8192);
        // frequency over each 1024-sample LFO step
        double lo = 1e9, hi = 0;
        for (size_t i = 0; i + 1024 <= s.size(); i += 1024) {
            std::vector<int32_t> w(s.begin() + (long)i, s.begin() + (long)i + 1024);
            double f = measure_freq(w);
            lo = std::min(lo, f); hi = std::max(hi, f);
        }
        double cents = 1200 * std::log2(hi / lo) / 2;
        std::printf("  vibrato DVB %d: +-%.1f cents (data sheet %s)\n", dvb, cents, dvb ? "14" : "7");
        CHECK(cents > (dvb ? 9.0 : 4.0) && cents < (dvb ? 16.0 : 8.0));
    }
}

static void test_rhythm() {
    std::printf("== chip: rhythm mode\n");
    const char *names[5] = {"BD", "SD", "TOM", "CY", "HH"};
    const uint8_t bits[5] = {0x10, 0x08, 0x04, 0x02, 0x01};
    for (int r = 0; r < 5; r++) {
        OplChip c;
        c.write(0x01, 0x20);
        for (int off : {0x10, 0x11, 0x12, 0x13, 0x14, 0x15}) {
            c.write((uint8_t)(0x20 + off), 0x21); c.write((uint8_t)(0x40 + off), 0x00);
            c.write((uint8_t)(0x60 + off), 0xf0); c.write((uint8_t)(0x80 + off), 0x00);
        }
        for (int ch = 6; ch < 9; ch++) {
            c.write((uint8_t)(0xa0 + ch), 0x80); c.write((uint8_t)(0xb0 + ch), (uint8_t)((4 << 2) | 1));
            c.write((uint8_t)(0xc0 + ch), 0x00);
        }
        c.write(0xbd, 0x20);                                                // rhythm on, nothing keyed
        auto s0 = run(c, 2000);
        CHECK_EQ(peak(s0), 0);
        c.write(0xbd, (uint8_t)(0x20 | bits[r]));
        int pk[5] = {0, 0, 0, 0, 0}, total = 0;
        for (int i = 0; i < 4000; i++) {
            total = std::max(total, std::abs(c.sample()));
            for (int k = 0; k < 5; k++) pk[k] = std::max(pk[k], std::abs(c.rhythm_output(k)));
        }
        std::printf("  %-3s: peaks BD %5d SD %5d TOM %5d CY %5d HH %5d\n", names[r], pk[0], pk[1], pk[2], pk[3], pk[4]);
        for (int k = 0; k < 5; k++) {
            if (k == r) CHECK(pk[k] > 2000);
            else CHECK_EQ(pk[k], 0);
        }
        CHECK(total > 2000);
        // the same bit without the rhythm bit: silence (and the rhythm keys are released)
        c.write(0xbd, bits[r]);
        for (int i = 0; i < 200; i++) c.sample();
        CHECK_EQ(c.rhythm_output(r), 0);
    }
    {   // without rhythm mode channel 6 plays as a melodic channel
        OplChip c;
        c.write(0x01, 0x20);
        c.write(0x33, 0x21); c.write(0x53, 0x00); c.write(0x73, 0xf0); c.write(0x93, 0x00);
        c.write(0x50, 0x3f); c.write(0x70, 0xf0);
        c.write(0xc6, 0x01); c.write(0xa6, 0x80); c.write(0xb6, 0x31);
        auto s = run(c, 2000);
        CHECK(peak(s) > 4000);
        CHECK(c.channel_output(6) != 0 || peak(s, 1990) > 0);
    }
}

static void test_timers() {
    std::printf("== chip: timers\n");
    OplChip c;
    c.write(0x02, 0xff);                                                    // timer 1: 1 x 80 us
    c.write(0x04, 0x01);
    int n = 0;
    while (!(c.read_status() & 0x40) && n < 1000) { c.sample(); n++; }
    std::printf("  timer 1 preset 0xff: overflow after %d samples (80 us = %.1f)\n", n, 80e-6 * RATE);
    CHECK(n >= 3 && n <= 5);
    CHECK((c.read_status() & 0x80) != 0);
    c.write(0x04, 0x80);
    CHECK_EQ(c.read_status(), 0);
    c.write(0x04, 0x60);                                                    // masked, stopped
    c.write(0x03, 0x00);                                                    // timer 2: 256 x 320 us
    c.write(0x04, 0x02);
    n = 0;
    while (!(c.read_status() & 0x20) && n < 10000) { c.sample(); n++; }
    // 256 x 4 x 288 clock cycles = 82.4 ms at 3.579545 MHz (the data sheet's 320 us is 1152 cycles rounded)
    std::printf("  timer 2 preset 0: overflow after %d samples (256 x 1152 cycles = 4096)\n", n);
    CHECK(n >= 4095 && n <= 4097);
}

// ---- 2. banks ----------------------------------------------------------------------------------------------
static std::vector<uint8_t> g_inst, g_drum;

static bool load_unpacked(const char *game_dir, const char *rel, std::vector<uint8_t> &out) {
    char path[1024];
    mc_path_join(path, sizeof path, game_dir, rel);
    mc_blob b{};
    if (!mc_read_unpacked(path, &b)) return false;
    out.assign(b.data, b.data + b.len);
    mc_blob_free(&b);
    return true;
}

static uint32_t rd32(const std::vector<uint8_t> &b, size_t o) { return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }
static uint32_t rd16(const std::vector<uint8_t> &b, size_t o) { return b[o] | (b[o + 1] << 8); }

static bool test_banks(const char *game_dir) {
    std::printf("== banks\n");
    if (!load_unpacked(game_dir, "data/inst.bnk", g_inst) || !load_unpacked(game_dir, "data/drum.bnk", g_drum)) {
        std::printf("  inst.bnk / drum.bnk missing\n");
        CHECK(false);
        return false;
    }
    for (int which = 0; which < 2; which++) {
        const std::vector<uint8_t> &b = which ? g_drum : g_inst;
        const char *nm = which ? "drum.bnk" : "inst.bnk";
        CHECK(b.size() >= 0x1c);
        CHECK(std::memcmp(b.data() + 2, "ADLIB-", 6) == 0);
        uint32_t used = rd16(b, 8), count = rd16(b, 10), offn = rd32(b, 12), offd = rd32(b, 16);
        std::printf("  %s: %zu bytes, version %d.%d, used %u, instruments %u, names at 0x%x, data at 0x%x\n", nm, b.size(), b[0], b[1], used, count, offn, offd);
        CHECK_EQ(count, 128); CHECK_EQ(used, 128);
        CHECK_EQ(offn, 0x1c); CHECK_EQ(offd, 0x1c + 128 * 12);
        CHECK_EQ(b.size(), offd + 128 * 30);
        int bad = 0;
        std::set<std::string> names;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *n = b.data() + offn + i * 12;
            CHECK_EQ(n[0] | (n[1] << 8), (int)i);                           // index = record
            char name[10] = {};
            std::memcpy(name, n + 3, 9);
            names.insert(name);
            const uint8_t *r = b.data() + offd + i * 30;
            for (int op = 0; op < 2; op++) {
                const uint8_t *p = r + 2 + op * 13;
                // ksl, mult, fb, ar, sl, eg, dr, rr, tl, am, vib, ksr, con
                const int lim[13] = {3, 15, 7, 15, 15, 1, 15, 15, 63, 1, 1, 1, 1};
                for (int k = 0; k < 13; k++) if (p[k] > lim[k]) bad++;
            }
            if (r[28] > 3 || r[29] > 3) bad++;
            if (i < 3 || (which && (i == 35 || i == 36 || i == 38)))
                std::printf("    %3u %-9s%s %s\n", i, name, which ? " pitch" : "", which ? std::to_string(n[2]).c_str() : "");
        }
        std::printf("    %zu distinct names, %d parameters out of range\n", names.size(), bad);
        CHECK_EQ(bad, 0);
    }
    // the driver's in-place packing (snd_opl_pack_timbre_bank_6955a), record 0 = piano1 worked out by hand
    HmiOplDriver d;
    d.set_timbre_bank(g_inst.data(), g_inst.size());
    d.set_timbre_bank(g_drum.data(), g_drum.size());
    CHECK(d.drum_bank_loaded());
    const uint8_t *t = d.melodic_timbre(0);
    CHECK(t != nullptr);
    if (t) {
        CHECK_EQ(t[0x0b], 0x01); CHECK_EQ(t[0x02], 0x4f); CHECK_EQ(t[0x05], 0xf1); CHECK_EQ(t[0x06], 0x53); CHECK_EQ(t[0x0e], 0x06);
        CHECK_EQ(t[0x18], 0x11); CHECK_EQ(t[0x0f], 0x00); CHECK_EQ(t[0x12], 0xd2); CHECK_EQ(t[0x13], 0x74);
        CHECK_EQ(t[0x1c], 0); CHECK_EQ(t[0x1d], 0);
    }
    // records 126 / 127 stay as in the file (count - 2 records are packed)
    for (int p = 125; p < 128; p++) {
        const uint8_t *tp = d.melodic_timbre(p), *raw = g_inst.data() + 0x61c + p * 30;
        bool same = tp && std::memcmp(tp, raw, 30) == 0;
        std::printf("  program %d: %s\n", p, same ? "unpacked (raw BNK parameters)" : "packed");
        CHECK(same == (p >= 126));
    }
    CHECK_EQ(d.drum_pitch(35), 12); CHECK_EQ(d.drum_pitch(36), 48); CHECK_EQ(d.drum_pitch(38), 60);
    return true;
}

// ---- 3. the driver -------------------------------------------------------------------------------------------
struct Rec : OplWriter {
    std::vector<std::pair<int, int>> w;
    void opl_write(uint8_t r, uint8_t v) override { w.push_back({r, v}); }
};
static void expect_writes(Rec &r, std::vector<std::pair<int, int>> want, const char *what) {
    bool ok = r.w == want;
    std::printf("  %-34s %2zu writes %s\n", what, r.w.size(), ok ? "as expected" : "DIFFER");
    if (!ok) {
        size_t n = std::max(r.w.size(), want.size());
        for (size_t i = 0; i < n; i++) {
            auto g = i < r.w.size() ? r.w[i] : std::make_pair(-1, -1), e = i < want.size() ? want[i] : std::make_pair(-1, -1);
            std::printf("    %2zu: got %02x=%02x want %02x=%02x%s\n", i, g.first & 0xff, g.second & 0xff, e.first & 0xff, e.second & 0xff, g == e ? "" : "  <--");
        }
        g_fail++;
    }
    r.w.clear();
}
static void ev(HmiOplDriver &d, int a, int b, int c) { uint8_t e[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; d.midi_event(e); }

static void test_driver() {
    std::printf("== driver\n");
    Rec r;
    HmiOplDriver d(&r);
    CHECK_EQ(d.init(0x220), 1);                                             // only 0x388 / 0x380 ...
    CHECK_EQ(r.w.size(), 10);                                               // ... but fn1 still clears the voices
    r.w.clear();
    CHECK_EQ(d.init(0x388), 0);
    {
        std::vector<std::pair<int, int>> want = {{0x01, 0x20}};
        for (int pass = 0; pass < 2; pass++) {                              // init_chip's and init's clear_voices
            for (int v = 0; v < 9; v++) want.push_back({0xb0 + v, 0});
            want.push_back({0xbd, 0});
        }
        expect_writes(r, want, "init (fn1)");
    }
    d.set_timbre_bank(g_inst.data(), g_inst.size());
    d.set_timbre_bank(g_drum.data(), g_drum.size());
    CHECK(r.w.empty());
    for (int v = 0; v < 9; v++) CHECK_EQ(d.program(v), 0);

    const std::vector<std::pair<int, int>> piano_mod_car = {
        {0x20, 0x01}, {0x40, 0x4f}, {0x60, 0xf1}, {0x80, 0x53}, {0xc0, 0x06}, {0xe0, 0x00},
        {0x23, 0x11}, {0x63, 0xd2}, {0x83, 0x74}, {0xe3, 0x00}};
    auto kill = [](int car, int mod, int vcar, int vmod) {
        std::vector<std::pair<int, int>> k;
        for (int i = 0; i < 5; i++) { k.push_back({0x80 + car, vcar}); k.push_back({0x80 + mod, vmod}); }
        return k;
    };
    auto cat = [](std::vector<std::pair<int, int>> a, const std::vector<std::pair<int, int>> &b) { a.insert(a.end(), b.begin(), b.end()); return a; };

    ev(d, 0xc0, 0, 0);
    CHECK(r.w.empty());
    ev(d, 0x90, 60, 127);                                                   // voice 0, C4 = block 4, F-number 0x157
    expect_writes(r, cat(cat(kill(3, 0, 0x0f, 0x0f), piano_mod_car), {{0x43, 0x00}, {0xa0, 0x57}, {0xb0, 0x11}, {0xb0, 0x31}}), "note on 60 vel 127");
    CHECK_EQ(d.voice_note(0), 60); CHECK_EQ(d.voice_channel(0), 0);
    ev(d, 0x90, 60, 0);                                                     // velocity 0 = note off
    expect_writes(r, {{0xb0, 0x11}}, "note off");
    CHECK_EQ(d.voice_note(0), 0);
    ev(d, 0x80, 60, 0);                                                     // nothing playing
    expect_writes(r, {}, "note off again");
    ev(d, 0x90, 60, 64);                                                    // level 13: vel_tab[32] = 13
    CHECK_EQ(g_opl_vel_level[32], 13);
    expect_writes(r, cat(cat(kill(3, 0, 0x7f, 0x5f), piano_mod_car), {{0x43, 0x0d}, {0xa0, 0x57}, {0xb0, 0x11}, {0xb0, 0x31}}), "note on 60 vel 64");
    ev(d, 0xb0, 7, 64);                                                     // x = 64, lv = 32, vel_tab[16] = 27 -> 27
    expect_writes(r, {{0x43, 27}}, "controller 7 = 64");
    CHECK_EQ(d.channel_volume(0), 64);
    ev(d, 0xe0, 0, 0x7f);                                                   // + 0.984 * (0x1181 - 0x1157) = +41
    expect_writes(r, {{0xa0, 0x80}, {0xb0, 0x31}}, "pitch bend up (msb 0x7f)");
    CHECK_EQ(d.calc_bent_freq(0x7f, 60, 0), 0x1180u);
    ev(d, 0xe0, 0, 0x00);                                                   // crosses the octave: (0x157 - 0x132) * .984 = 36
    expect_writes(r, {{0xa0, 0x33}, {0xb0, 0x31}}, "pitch bend down (msb 0)");
    CHECK_EQ(d.calc_bent_freq(0x40, 60, 0), 0x1157u);
    CHECK_EQ(d.calc_bent_freq(0x60, 69, 0), 0x1241u + (0x1287u - 0x1241u) * ((32u * 1000u) >> 6) / 1000u);   // A4 -> B4 half way
    ev(d, 0xe0, 0, 0x40);
    expect_writes(r, {{0xa0, 0x57}, {0xb0, 0x31}}, "pitch bend centre");

    // drum: channel 9 note 36 = drum.bnk record 36 (SBBD), pitch byte 48 -> C3 = 0x0d57, voice 1
    ev(d, 0x99, 36, 127);
    expect_writes(r, cat(kill(4, 1, 0x0f, 0x0f), {{0x21, 0x00}, {0x41, 0x0b}, {0x61, 0xa8}, {0x81, 0x4c}, {0xc1, 0x00}, {0xe1, 0x00},
                                                  {0x24, 0x00}, {0x64, 0xd6}, {0x84, 0x4f}, {0xe4, 0x00}, {0x44, 0x00},
                                                  {0xa1, 0x57}, {0xb1, 0x0d}, {0xb1, 0x2d}}), "drum note 36 (channel 10)");
    CHECK_EQ(d.voice_note(1), 36); CHECK_EQ(d.voice_channel(1), 9);
    ev(d, 0x89, 36, 0);
    expect_writes(r, {{0xb1, 0x0d}}, "drum note off");

    // FM timbre with FB / CON byte 0 (program 9 "ominous2": FB 2? -> check): the modulator gets the level too
    {
        int prog = -1;
        for (int p = 0; p < 126; p++) if (d.melodic_timbre(p)[0x0e] == 0) { prog = p; break; }
        CHECK(prog >= 0);
        ev(d, 0xc2, prog, 0);
        ev(d, 0x92, 72, 127);                                               // voice 1 (free again)
        bool mod_level = false;
        for (auto &w : r.w) if (w.first == 0x41) mod_level = true;          // 0x41 written twice: timbre + level
        int n41 = 0;
        for (auto &w : r.w) n41 += w.first == 0x41;
        std::printf("  program %d (FB/CON byte 0): modulator level written %d times\n", prog, n41);
        CHECK(mod_level); CHECK_EQ(n41, 2);
        r.w.clear();
        ev(d, 0x82, 72, 0);
        r.w.clear();
    }

    // voice allocation: 9 voices busy, then stealing from the lowest channel without a pitch bend
    for (int ch = 1; ch <= 8; ch++) ev(d, 0x90 | ch, 50 + ch, 100);        // voices 1..8 (voice 0 holds C4 on ch 0)
    for (int v = 0; v < 9; v++) CHECK(d.voice_note(v) != 0);
    CHECK_EQ(d.voice_channel(1), 1);
    ev(d, 0x93, 80, 100);                                                   // channel 0 had a bend -> channel 1's voice
    CHECK_EQ(d.voice_note(1), 80); CHECK_EQ(d.voice_channel(1), 3);
    ev(d, 0xe0 | 1, 0, 0x40); ev(d, 0xe0 | 2, 0, 0x40); ev(d, 0xe0 | 3, 0, 0x40); ev(d, 0xe0 | 4, 0, 0x40);
    ev(d, 0x9a, 81, 100);                                                   // channels 0..4 bent: channel 5's voice (5)
    CHECK_EQ(d.voice_note(5), 81); CHECK_EQ(d.voice_channel(5), 10);
    for (int ch = 5; ch < 16; ch++) ev(d, 0xe0 | ch, 0, 0x40);
    ev(d, 0x9c, 82, 100);                                                   // every channel bent: voice = 12 - 9 = 3
    CHECK_EQ(d.voice_note(3), 82); CHECK_EQ(d.voice_channel(3), 12);
    r.w.clear();

    // controller 0x7b on channel 0: key off voice 0 (C4, shadow 0x31 -> 0x11)
    ev(d, 0xb0, 0x7b, 0);
    expect_writes(r, {{0xb0, 0x11}}, "all notes off (channel 0)");
    CHECK_EQ(d.voice_note(0), 0);
    ev(d, 0xb0, 0x79, 0);
    expect_writes(r, {}, "reset controllers (channel 0)");
    CHECK_EQ(d.channel_volume(0), 0x7f);

    // fn3 reset: rhythm register, keys off from the shadows, carrier levels 0xff
    d.reset();
    {
        std::vector<std::pair<int, int>> want = {{0xbd, 0}};
        const int sh[9] = {0x11, 0x0d, 0x08, 0x0c, 0x09, 0x08, 0x08, 0x08, 0x08};
        (void)sh;
        CHECK(r.w.size() == 1 + 9 + 9);
        if (r.w.size() == 19) {
            CHECK(r.w[0] == std::make_pair(0xbd, 0));
            for (int v = 0; v < 9; v++) { CHECK_EQ(r.w[1 + v].first, 0xb0 + v); CHECK_EQ(r.w[1 + v].second & 0x20, 0); }
            const int car[9] = {3, 4, 5, 11, 12, 13, 19, 20, 21};
            for (int v = 0; v < 9; v++) { CHECK_EQ(r.w[10 + v].first, 0x40 + car[v]); CHECK_EQ(r.w[10 + v].second, 0xff); }
        }
        std::printf("  reset (fn3)                        %2zu writes\n", r.w.size());
        r.w.clear();
        for (int v = 0; v < 9; v++) { CHECK_EQ(d.voice_note(v), 0); CHECK_EQ(d.voice_channel(v), 0); }
        for (int ch = 0; ch < 16; ch++) CHECK_EQ(d.program(ch), 0);
    }
    // sustain: note-offs queue while controller 0x40 is on
    ev(d, 0x90, 60, 100);
    r.w.clear();
    ev(d, 0xb0, 0x40, 127);
    ev(d, 0x80, 60, 0);
    CHECK(r.w.empty()); CHECK_EQ(d.voice_note(0), 60);
    ev(d, 0xb0, 0x40, 0);                                                   // replays entry [1] (sic), not [0]
    CHECK_EQ(d.voice_note(0), 60);
    ev(d, 0xb0, 0x7b, 0);
    CHECK_EQ(d.voice_note(0), 0);
}

// ---- 4. songs -----------------------------------------------------------------------------------------------
struct Spy : MidiOut {
    OplMidiOut *o = nullptr;
    std::set<int> programs[16];
    int note_lo[16], note_hi[16];
    std::map<int, int> ccs;
    int note_ons = 0, bends = 0;
    Spy() { for (int i = 0; i < 16; i++) { note_lo[i] = 999; note_hi[i] = -1; } }
    void send(uint8_t st, uint8_t d1, uint8_t d2) override {
        int ch = st & 15;
        switch (st & 0xf0) {
        case 0x90: if (d2) { note_ons++; note_lo[ch] = std::min(note_lo[ch], (int)d1); note_hi[ch] = std::max(note_hi[ch], (int)d1); } break;
        case 0xb0: ccs[d1]++; break;
        case 0xc0: programs[ch].insert(d1); break;
        case 0xe0: bends++; break;
        default: break;
        }
        o->send(st, d1, d2);
    }
    void reset() override { o->reset(); }
    void panic() override { o->panic(); }
    bool renders() const override { return true; }
    void render(int32_t *lr, int frames, int rate) override { o->render(lr, frames, rate); }
};

static void render_songs(const char *game_dir) {
    std::printf("== songs (FM banks through the OPL2 driver)\n");
    const int OUT_RATE = 44100;
    const int seconds = g_out.empty() ? 15 : 60;
    for (int set = 0; set < 2; set++) {
        mc_sndbank b;
        if (!mc_sndbank_load_named(game_dir, "music", set, 0, 0, &b)) { std::printf("  music%d-0 missing\n", set); CHECK(false); continue; }
        CHECK_EQ(mc_sndbank_count(&b), set == 0 ? 4 : 3);
        for (int i = 1; i <= mc_sndbank_count(&b); i++) {
            uint32_t len = 0;
            const uint8_t *p = mc_sndbank_record_data(&b, i, &len);
            OplMidiOut out;
            CHECK(out.load_banks(g_inst.data(), g_inst.size(), g_drum.data(), g_drum.size()));
            Spy spy; spy.o = &out;
            AudioEngine e;
            e.set_output_rate(OUT_RATE);
            e.set_midi(&spy, AudioEngine::CLOCK_RENDER);
            e.set_music_device(MC_HMP_DEVICE_OPL2);
            CHECK(e.music_play(i, p, len));
            int tracks_on = 0;
            for (uint32_t t = 0; t < e.sequencer().song().track_count; t++) tracks_on += mc_hmp_track_plays(&e.sequencer().song(), (int)t, MC_HMP_DEVICE_OPL2);
            std::vector<int16_t> pcm((size_t)OUT_RATE * seconds * 2);
            const int CH = 441;                                             // 10 ms chunks
            int max_voices = 0;
            double sum_voices = 0;
            int chunks = 0;
            for (int f = 0; f < OUT_RATE * seconds; f += CH) {
                e.render(pcm.data() + 2 * (size_t)f, std::min(CH, OUT_RATE * seconds - f));
                int nv = 0;
                for (int v = 0; v < 9; v++) nv += out.driver().voice_note(v) != 0;
                max_voices = std::max(max_voices, nv);
                sum_voices += nv; chunks++;
            }
            double sq = 0; int pk = 0; long clipped = 0;
            for (int16_t s : pcm) { sq += (double)s * s; pk = std::max(pk, std::abs((int)s)); if (s >= 32767 || s <= -32767) clipped++; }
            double rms = std::sqrt(sq / (double)pcm.size());
            std::string progs, notes;
            for (int ch = 0; ch < 16; ch++) {
                for (int pr : spy.programs[ch]) progs += std::to_string(ch + 1) + ":" + std::to_string(pr) + " ";
                if (spy.note_hi[ch] >= 0) notes += std::to_string(ch + 1) + ":" + std::to_string(spy.note_lo[ch]) + "-" + std::to_string(spy.note_hi[ch]) + " ";
            }
            std::string ccs;
            for (auto &kv : spy.ccs) ccs += std::to_string(kv.first) + "x" + std::to_string(kv.second) + " ";
            std::printf("  music%d-0 %d %-13s tracks %2d/%2u  %ds: notes %5d, bends %3d, voices max %d avg %.1f, %llu writes, RMS %6.0f peak %5d clipped %ld\n",
                        set, i, mc_sndbank_name(&b, i), tracks_on, e.sequencer().song().track_count, seconds, spy.note_ons, spy.bends,
                        max_voices, sum_voices / chunks, (unsigned long long)out.writes(), rms, pk, clipped);
            std::printf("      programs %s\n      notes %s\n      controllers %s\n", progs.c_str(), notes.c_str(), ccs.c_str());
            CHECK(spy.note_ons > 20);
            CHECK(rms > 300);
            CHECK(clipped < (long)(pcm.size() / 1000));
            for (int ch = 0; ch < 16; ch++) {
                if (spy.note_hi[ch] < 0) continue;
                if (ch != 9) CHECK(spy.note_lo[ch] >= 12);                  // notes 0..11 would read the velocity table
                if (ch != 9) for (int pr : spy.programs[ch]) CHECK(pr < 126);   // 126 / 127 are unpacked in the original
            }
            if (!g_out.empty()) {
                char name[1024];
                std::snprintf(name, sizeof name, "%s/music%d-0-%d.wav", g_out.c_str(), set, i);
                CHECK(audio_write_wav(name, pcm.data(), pcm.size() / 2, 2, OUT_RATE));
                std::printf("      -> %s\n", name);
            }
        }
        mc_sndbank_free(&b);
    }
}

// ---- 4b. the streams for the comparison with the original driver code ---------------------------------------
// With argv[2]: per song <out>/music<set>-0-<n>.events (I = fn1 init, B = fn4 bank, E = fn0 event, R = fn3
// reset) and .regs (the port driver's register writes, reg / value bytes). The whole song is played, the
// master / layer volume changed on the way, the song restarted and stopped. opl_orig.py (scratch) runs the
// original snd_opl_* code on the .events in a CPU emulator and compares the writes.
struct Tee : MidiOut {
    HmiOplDriver *d = nullptr;
    FILE *f = nullptr;
    void send(uint8_t st, uint8_t d1, uint8_t d2) override {
        std::fprintf(f, "E %02x %02x %02x\n", st, d1, d2);
        uint8_t e[3] = {st, d1, d2};
        d->midi_event(e);
    }
    void reset() override { std::fprintf(f, "R\n"); d->reset(); }
};
static void dump_streams(const char *game_dir) {
    if (g_out.empty()) return;
    std::printf("== event / register streams for opl_orig.py\n");
    for (int set = 0; set < 2; set++) {
        mc_sndbank b;
        if (!mc_sndbank_load_named(game_dir, "music", set, 0, 0, &b)) continue;
        for (int i = 1; i <= mc_sndbank_count(&b); i++) {
            uint32_t len = 0;
            const uint8_t *p = mc_sndbank_record_data(&b, i, &len);
            char name[1024];
            std::snprintf(name, sizeof name, "%s/music%d-0-%d.events", g_out.c_str(), set, i);
            FILE *f = std::fopen(name, "w");
            if (!f) { CHECK(false); continue; }
            Rec rec;
            HmiOplDriver d(&rec);
            Tee tee; tee.d = &d; tee.f = f;
            std::fprintf(f, "I 388\n"); d.init(0x388);
            std::fprintf(f, "B inst\n"); d.set_timbre_bank(g_inst.data(), g_inst.size());
            std::fprintf(f, "B drum\n"); d.set_timbre_bank(g_drum.data(), g_drum.size());
            MusicSequencer seq;
            seq.set_output(&tee);
            seq.set_device(MC_HMP_DEVICE_OPL2);
            CHECK(seq.load_and_start(p, len));
            uint32_t ticks = 0;
            while (!seq.done() && ticks < 120u * 600u) {
                seq.tick();
                ticks++;
                if (ticks == 3000) seq.set_master_volume(0x60);
                if (ticks == 3100) seq.set_layer_volume(0x50);
                if (ticks == 6000) seq.set_master_volume(0x7f);
            }
            CHECK(seq.done());
            CHECK(seq.load_and_start(p, len));                          // music_update restarts the song
            for (int k = 0; k < 2400; k++) seq.tick();
            seq.stop();                                                 // 16 x (79, 7B) + fn3 reset
            std::fclose(f);
            std::snprintf(name, sizeof name, "%s/music%d-0-%d.regs", g_out.c_str(), set, i);
            FILE *g = std::fopen(name, "wb");
            if (!g) { CHECK(false); continue; }
            for (auto &w : rec.w) { std::fputc(w.first, g); std::fputc(w.second, g); }
            std::fclose(g);
            std::printf("  music%d-0-%d: %u ticks, %zu register writes\n", set, i, ticks, rec.w.size());
        }
        mc_sndbank_free(&b);
    }
    // a random stream over every path the songs do not take: all 16 channels, programs 0..127 (126 / 127
    // unpacked), drums, velocity 0, controllers 7 / 0x40 sustain / 0x66 bend range / 0x79 / 0x7b, pitch
    // bends, voice stealing, resets. Notes 12..115 and bend ranges 0..12 keep every table index inside the
    // extracted tables (outside them the original reads unrelated memory; notes < 12 with a pending bend
    // would make it loop ~357M times).
    {
        char name[1024];
        std::snprintf(name, sizeof name, "%s/fuzz.events", g_out.c_str());
        FILE *f = std::fopen(name, "w");
        if (!f) { CHECK(false); return; }
        Rec rec;
        HmiOplDriver d(&rec);
        Tee tee; tee.d = &d; tee.f = f;
        std::fprintf(f, "I 388\n"); d.init(0x388);
        std::fprintf(f, "B inst\n"); d.set_timbre_bank(g_inst.data(), g_inst.size());
        std::fprintf(f, "B drum\n"); d.set_timbre_bank(g_drum.data(), g_drum.size());
        uint32_t rng = 12345;
        auto rnd = [&rng](uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; };
        for (int k = 0; k < 40000; k++) {
            uint8_t ch = (uint8_t)rnd(16);
            uint32_t r = rnd(100);
            if (r < 40) tee.send((uint8_t)(0x90 | ch), (uint8_t)(12 + rnd(104)), (uint8_t)(rnd(8) == 0 ? 0 : rnd(128)));
            else if (r < 65) tee.send((uint8_t)(0x80 | ch), (uint8_t)(12 + rnd(104)), (uint8_t)rnd(128));
            else if (r < 72) tee.send((uint8_t)(0xc0 | ch), (uint8_t)rnd(128), 0);
            else if (r < 82) tee.send((uint8_t)(0xe0 | ch), (uint8_t)rnd(128), (uint8_t)rnd(128));
            else if (r < 88) tee.send((uint8_t)(0xb0 | ch), 7, (uint8_t)rnd(128));
            else if (r < 92) {                                              // sustain on one channel at a time
                uint8_t sc = (uint8_t)(k & 1 ? 2 : 5);                      // (two sustained channels can hang the original)
                tee.send((uint8_t)(0xb0 | sc), 0x40, 127);
                for (int m = 0; m < 4; m++) tee.send((uint8_t)(0x80 | sc), (uint8_t)(12 + rnd(104)), 0);
                tee.send((uint8_t)(0xb0 | sc), 0x40, 0);
            }
            else if (r < 94) tee.send((uint8_t)(0xb0 | ch), 0x66, (uint8_t)rnd(13));
            else if (r < 96) tee.send((uint8_t)(0xb0 | ch), 0x79, 0);
            else if (r < 98) tee.send((uint8_t)(0xb0 | ch), 0x7b, 0);
            else if (r < 99) tee.send((uint8_t)(0xb0 | ch), 10, (uint8_t)rnd(128));
            else if (rnd(10) == 0) tee.reset();
        }
        std::fclose(f);
        std::snprintf(name, sizeof name, "%s/fuzz.regs", g_out.c_str());
        FILE *g = std::fopen(name, "wb");
        if (!g) { CHECK(false); return; }
        for (auto &w : rec.w) { std::fputc(w.first, g); std::fputc(w.second, g); }
        std::fclose(g);
        std::printf("  fuzz: 40000 events, %zu register writes\n", rec.w.size());
    }
}

// ---- 5. the game side --------------------------------------------------------------------------------------
static void test_game_side(const char *game_dir) {
    std::printf("== sound.cpp with g_music_device 0\n");
    OplMidiOut out;
    CHECK(out.load_banks(g_inst.data(), g_inst.size(), g_drum.data(), g_drum.size()));
    AudioEngine e;
    e.set_output_rate(22050);
    e.set_midi(&out, AudioEngine::CLOCK_RENDER);
    e.set_music_device(MC_HMP_DEVICE_OPL2);
    sound_set_backend(&e);
    g_music_available = 1; g_music_on = 1; g_music_device = 0;
    CHECK(music_load_bank(game_dir, 0));
    CHECK_EQ(g_music_track_count, 4);
    music_play_track(2);
    CHECK_EQ(g_music_track, 2);
    CHECK(!e.music_done());
    std::vector<int16_t> buf(2 * 2205);
    int sounding = 0;
    for (int k = 0; k < 40; k++) {                                          // 4 s
        e.render(buf.data(), 2205);
        for (int i = 0; i < 2 * 2205; i++) if (buf[(size_t)i]) { sounding++; break; }
    }
    std::printf("  CGAME2.HMP: %d of 40 chunks with sound, %llu register writes\n", sounding, (unsigned long long)out.writes());
    CHECK(sounding > 30);
    music_stop();                                                           // ... 16 x (79, 7B), then the driver reset
    CHECK(e.music_done());
    for (int v = 0; v < 9; v++) CHECK_EQ(out.driver().voice_note(v), 0);
    for (int k = 0; k < 20; k++) e.render(buf.data(), 2205);              // the releases die out
    e.render(buf.data(), 2205);
    int pk = 0;
    for (int16_t s : buf) pk = std::max(pk, std::abs((int)s));
    std::printf("  after music_stop: peak %d\n", pk);
    CHECK(pk < 64);
    sound_set_backend(nullptr);
    g_music_available = g_music_on = 0;
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    g_out = argc > 2 ? argv[2] : "";
    test_frequency();
    test_envelope();
    test_levels();
    test_waveforms();
    test_feedback();
    test_lfo();
    test_rhythm();
    test_timers();
    if (test_banks(game_dir)) {
        test_driver();
        render_songs(game_dir);
        dump_streams(game_dir);
        test_game_side(game_dir);
    }
    std::printf("%s: %d failure(s)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
