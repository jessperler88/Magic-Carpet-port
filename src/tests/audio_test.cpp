// Platform audio test (port round 5, task A), no SDL:
//  1. AudioMixer by construction: volume / pan law, the HMI flags, linear interpolation, resampled
//     length, loop counts, the stop / volume ramps, the 32-voice limit and stale handles, master gain;
//  2. mcdata/hmp: every track of every music bank (music<set>-<device>, set 0 / 1, device 0 / 1 / 2)
//     parsed: event counts, note-ons, rate, duration against the header's seconds, no event the HMI
//     sequencer could not step over; the GM banks written as standard MIDI files (argv[2]);
//  3. MusicSequencer against a recording MIDI output: timing equals the parsed ticks, song end (reset
//     messages, done, rewind), master volume scaling of controller 7, the layer volume, stop; and the
//     game-side loop: sound.cpp's music_update restarting the song through AudioEngine;
//  4. offline renders into argv[2] (if given): the first 60 s of movie 0 played from its snapshot with
//     AudioEngine as the backend (samples + the music through the square-wave synth) -> movie0.wav, and
//     every General MIDI level / intro track through the square synth -> music<set>-2-<n>.wav.
// argv[1] = game dir, argv[2] = output directory for .wav / .mid (optional).
#include "audio_mixer.h"
#include "sim.h"
#include "sound.h"
#include "sndbank.h"
#include "hmp.h"
#include "thing.h"
#include "player.h"
#include "demo.h"
#include "crash_handler.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { \
    std::printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); g_fail++; } } while (0)

static std::string g_out;           // output directory ("" = write nothing)
static std::string out_path(const std::string &name) { return g_out + "/" + name; }

// ---- 1. mixer ------------------------------------------------------------------------------------------
// Mix `frames` frames of one mixer into a fresh accumulator.
static std::vector<int32_t> mix(AudioMixer &m, int frames) {
    std::vector<int32_t> acc((size_t)frames * 2, 0);
    m.mix(acc.data(), frames);
    return acc;
}
static int count_nonzero_frames(const std::vector<int32_t> &a) {
    int n = 0;
    for (size_t i = 0; i < a.size(); i += 2) if (a[i] || a[i + 1]) n++;
    return n;
}

static void test_mixer() {
    std::printf("== mixer\n");
    std::vector<uint8_t> loud(5000, 0xff);                  // (0xff - 128) << 8 = 32512
    const int S = 32512;
    {   // volume / pan law
        AudioMixer m; m.set_output_rate(22050);
        int h = m.start(loud.data(), 100, 22050, 0x7fff, 0x7fff, 0, 0x300);
        CHECK(h >= 0); CHECK(m.is_playing(h));
        auto a = mix(m, 1);
        CHECK_EQ(a[0], (S * 32767) >> 15); CHECK_EQ(a[1], (S * 32767) >> 15);       // centre = unity both sides
        int gl, gr;
        AudioMixer::pan_gains(0x7fff, 0, &gl, &gr);      CHECK_EQ(gl, 32767); CHECK_EQ(gr, 0);
        AudioMixer::pan_gains(0x7fff, 0xffff, &gl, &gr); CHECK_EQ(gl, 0);     CHECK_EQ(gr, 32767);
        AudioMixer::pan_gains(0x7fff, 0x4000, &gl, &gr); CHECK_EQ(gl, 32767); CHECK_EQ(gr, (32767 * 32770) >> 16);
        AudioMixer::pan_gains(0x7fff, 0xc000, &gl, &gr); CHECK_EQ(gl, (32767 * 32766) >> 16); CHECK_EQ(gr, 32767);
        AudioMixer::pan_gains(0x4000, 0x7fff, &gl, &gr); CHECK_EQ(gl, 0x4000); CHECK_EQ(gr, 0x4000);
        AudioMixer::pan_gains(-5, 0x7fff, &gl, &gr);     CHECK_EQ(gl, 0);      CHECK_EQ(gr, 0);
        AudioMixer::pan_gains(0x12345, 0x7fff, &gl, &gr); CHECK_EQ(gl, 32767);   // clamped to 0x7fff
        m.stop_all_now();
        h = m.start(loud.data(), 100, 22050, 0x4000, 0xffff, 0, 0x300);           // half volume, hard right
        a = mix(m, 1);
        CHECK_EQ(a[0], 0); CHECK_EQ(a[1], (S * 0x4000) >> 15);
        m.stop_all_now();
        h = m.start(loud.data(), 100, 22050, 0x4000, 0xffff, 0, 0x100);           // no _PANNING: centre
        a = mix(m, 1);
        CHECK_EQ(a[0], (S * 0x4000) >> 15); CHECK_EQ(a[1], (S * 0x4000) >> 15);
        m.stop_all_now();
        h = m.start(loud.data(), 100, 22050, 0x1000, 0x7fff, 0, 0x200);           // no _VOLUME: full volume
        a = mix(m, 1);
        CHECK_EQ(a[0], (S * 32767) >> 15);
        m.stop_all_now();
    }
    {   // resampled length (linear interpolation, 16.16 step)
        struct Case { int src, out, frames; } cases[] = { {22050, 44100, 2000}, {11025, 44100, 4000}, {22050, 22050, 1000},
                                                          {22050, 48000, 2177}, {11025, 48000, 4354}, {22050, 11025, 500} };
        for (auto &c : cases) {
            AudioMixer m; m.set_output_rate(c.out);
            int h = m.start(loud.data(), 1000, c.src, 0x7fff, 0x7fff, 0, 0x300);
            auto a = mix(m, 6000);
            CHECK_EQ(count_nonzero_frames(a), c.frames);
            CHECK(!m.is_playing(h)); CHECK_EQ(m.sounding(), 0);
            std::printf("  1000 samples at %5d Hz -> %d frames at %d Hz\n", c.src, count_nonzero_frames(a), c.out);
        }
    }
    {   // linear interpolation: 0x80, 0xc0 at half the output rate
        static const uint8_t two[2] = { 0x80, 0xc0 };
        AudioMixer m; m.set_output_rate(44100);
        m.start(two, 2, 22050, 0x7fff, 0x7fff, 0, 0x300);
        auto a = mix(m, 6);
        CHECK_EQ(a[0], 0);
        CHECK_EQ(a[2], (8192 * 32767) >> 15);              // halfway between 0 and 0x4000
        CHECK_EQ(a[4], (16384 * 32767) >> 15);
        CHECK_EQ(a[6], (16384 * 32767) >> 15);             // last sample held (one-shot: no wrap to sample 0)
        CHECK_EQ(a[8], 0);
    }
    {   // loops
        AudioMixer m; m.set_output_rate(22050);
        int h = m.start(loud.data(), 100, 22050, 0x7fff, 0x7fff, 2, 0x4300);       // 2 further passes
        CHECK_EQ(count_nonzero_frames(mix(m, 1000)), 300); CHECK(!m.is_playing(h));
        h = m.start(loud.data(), 100, 22050, 0x7fff, 0x7fff, 5, 0x300);            // loop count without _LOOPING
        CHECK_EQ(count_nonzero_frames(mix(m, 1000)), 100);
        h = m.start(loud.data(), 100, 22050, 0x7fff, 0x7fff, -1, 0x4300);          // forever
        CHECK_EQ(count_nonzero_frames(mix(m, 100000)), 100000); CHECK(m.is_playing(h));
        // a looping voice interpolates across the loop point: last sample -> first sample
        static const uint8_t saw[2] = { 0x80, 0xc0 };
        AudioMixer m2; m2.set_output_rate(44100);
        m2.start(saw, 2, 22050, 0x7fff, 0x7fff, -1, 0x4300);
        auto a = mix(m2, 4);
        CHECK_EQ(a[6], (8192 * 32767) >> 15);              // halfway from 0x4000 back to 0
    }
    {   // stop ramp: no click, not playing at once, slot freed after the ramp
        AudioMixer m; m.set_output_rate(44100);
        int h = m.start(loud.data(), 5000, 44100, 0x7fff, 0x7fff, 0, 0x300);
        mix(m, 10);
        m.stop(h);
        CHECK(!m.is_playing(h)); CHECK_EQ(m.playing(), 0); CHECK_EQ(m.sounding(), 1);
        auto a = mix(m, 300);
        int max_step = 0, last = (S * 32767) >> 15, ramp_frames = count_nonzero_frames(a);
        for (int f = 0; f < 300; f++) { max_step = (std::max)(max_step, std::abs(a[2 * (size_t)f] - last)); last = a[2 * (size_t)f]; }
        std::printf("  stop ramp: %d frames, largest step %d (full scale 32512)\n", ramp_frames, max_step);
        CHECK(ramp_frames <= AudioMixer::RAMP_SAMPLES); CHECK(ramp_frames >= AudioMixer::RAMP_SAMPLES - 2);
        CHECK(max_step <= (S * (0x8000 / AudioMixer::RAMP_SAMPLES)) / 32768 + 1);
        CHECK_EQ(m.sounding(), 0);
        m.stop(h);                                          // stale handle: harmless
        // volume change ramps too
        h = m.start(loud.data(), 5000, 44100, 0x7fff, 0x7fff, 0, 0x300);
        mix(m, 1);
        m.set_volume(h, 0x4000);
        a = mix(m, 200);
        CHECK(a[0] < (S * 32767) >> 15 && a[0] > (S * 0x4000) >> 15);
        CHECK_EQ(a[2 * 199], (S * 0x4000) >> 15);
        m.set_volume(h, -1);                                // (0 << 8) - 1 from sound_set_sample_volume: clamped to 0
        a = mix(m, 200);
        CHECK_EQ(a[2 * 199], 0); CHECK(m.is_playing(h));    // silent but still playing
    }
    {   // 32 voices, handles go stale on reuse
        AudioMixer m; m.set_output_rate(22050);
        int hs[32];
        for (int i = 0; i < 32; i++) { hs[i] = m.start(loud.data(), 1000, 22050, 0x100, 0x7fff, 0, 0x300); CHECK(hs[i] >= 0); }
        CHECK_EQ(m.start(loud.data(), 1000, 22050, 0x100, 0x7fff, 0, 0x300), -1);
        m.stop(hs[5]);
        int h = m.start(loud.data(), 1000, 22050, 0x100, 0x7fff, 0, 0x300);
        CHECK(h >= 0); CHECK(h != hs[5]); CHECK(!m.is_playing(hs[5])); CHECK(m.is_playing(h));
        CHECK_EQ(m.playing(), 32); CHECK_EQ(m.sounding(), 33);                    // + the stop ramp of hs[5]
        CHECK_EQ(m.start(nullptr, 10, 22050, 0x7fff, 0x7fff, 0, 0x300), -1);
        m.stop_all_now(); CHECK_EQ(m.sounding(), 0); CHECK(!m.is_playing(h));
        CHECK_EQ(m.start(loud.data(), 0, 22050, 0x7fff, 0x7fff, 0, 0x300), -1);   // NULL.RAW: 16 - 16 bytes
    }
    {   // AudioEngine: master gain 0x80 and clipping to int16
        AudioEngine e; e.set_output_rate(22050);
        int h = e.start(0, 1, loud.data(), 100, 22050, 0x7fff, 0x7fff, 0, 0x300);
        int16_t out[4];
        e.render(out, 2);
        CHECK_EQ(out[0], (((S * 32767) >> 15) * 0x60) >> 8);
        CHECK(e.is_playing(h));
        for (int i = 0; i < 7; i++) e.start(0, 1, loud.data(), 100, 22050, 0x7fff, 0x7fff, 0, 0x300);
        e.render(out, 2);
        int32_t sum8 = 8 * ((((S * 32767) >> 15) * 0x60) >> 8);
        CHECK_EQ(out[0], 24576 + (int64_t)(sum8 - 24576) * 8191 / ((sum8 - 24576) + 8191));   // soft knee
        CHECK(out[0] < 32767 && out[0] > 31000);
        CHECK_EQ(AudioEngine::soft_clip(24576), 24576); CHECK_EQ(AudioEngine::soft_clip(-24577), -24576);
        CHECK_EQ(AudioEngine::soft_clip(1 << 30), 32766); CHECK_EQ(AudioEngine::soft_clip(-(1 << 30)), -32766);
        e.flush();
        CHECK(!e.is_playing(h)); CHECK_EQ(e.mixer().sounding(), 0);
    }
}

// ---- 2. HMP parsing -----------------------------------------------------------------------------------
struct BankTrack { int set, dev, index; std::string name; std::vector<uint8_t> bytes; };
static std::vector<BankTrack> g_tracks;

static void test_hmp(const char *game_dir) {
    std::printf("== HMP: every track of every music bank\n");
    std::printf("  bank       n  name           trk  events  notes rate   ticks  seconds(hdr)  playing on GM / OPL\n");
    int banks = 0;
    for (int set = 0; set <= 1; set++) {
        for (int dev = 0; dev <= 2; dev++) {
            mc_sndbank b;
            if (!mc_sndbank_load_named(game_dir, "music", set, dev, 0, &b)) { std::printf("  music%d-%d missing\n", set, dev); CHECK(false); continue; }
            banks++;
            CHECK_EQ(mc_sndbank_count(&b), set == 0 ? 4 : 3);
            for (int i = 1; i <= mc_sndbank_count(&b); i++) {
                uint32_t len = 0;
                const uint8_t *p = mc_sndbank_record_data(&b, i, &len);
                CHECK(p != nullptr);
                mc_hmp_song s;
                int ok = mc_hmp_parse(p, len, &s);
                CHECK(ok);
                if (!ok) continue;
                uint32_t events = 0;
                int gm = 0, opl = 0, ends = 0;
                for (uint32_t t = 0; t < s.track_count; t++) {
                    events += s.tracks[t].event_count;
                    ends += s.tracks[t].has_end;
                    gm += mc_hmp_track_plays(&s, (int)t, MC_HMP_DEVICE_MPU401);
                    opl += mc_hmp_track_plays(&s, (int)t, MC_HMP_DEVICE_OPL2);
                }
                double secs = (double)s.duration_ticks / s.rate;
                std::printf("  music%d-%d  %d  %-13s %3u %7u %6u %4u %7u %7.2f (%3u)    %d / %d\n", set, dev, i, mc_sndbank_name(&b, i),
                            s.track_count, events, s.note_ons, s.rate, s.duration_ticks, secs, s.seconds, gm, opl);
                CHECK_EQ(s.bad_events, 0);
                CHECK_EQ(ends, (int)s.track_count);
                CHECK_EQ(s.rate, 120);
                CHECK_EQ(s.division, 120);
                CHECK(std::fabs(secs - s.seconds) <= 1.0);
                CHECK(s.bytes_used <= len);
                CHECK_EQ(gm, (int)s.track_count);
                CHECK(s.note_ons > 0);
                if (!g_out.empty() && dev == 2) {
                    char name[96];
                    std::snprintf(name, sizeof name, "music%d-%d-%d.mid", set, dev, i);
                    CHECK(mc_hmp_write_midi(&s, MC_HMP_DEVICE_MPU401, out_path(name).c_str()));
                }
                g_tracks.push_back({set, dev, i, mc_sndbank_name(&b, i), std::vector<uint8_t>(p, p + len)});
                mc_hmp_free(&s);
            }
            mc_sndbank_free(&b);
        }
    }
    CHECK_EQ(banks, 6);
    // the -0x10 of the sample players would cut CSETUP.ROL short: its last chunk ends in the padding
    for (auto &t : g_tracks) {
        if (t.set != 0 || t.dev != 1 || t.index != 4) continue;
        mc_hmp_song s;
        CHECK(!mc_hmp_parse(t.bytes.data(), t.bytes.size() - 0x10, &s));
    }
    // varlen / lengths by construction
    static const uint8_t v1[] = { 0x80 }, v2[] = { 0x5f, 0x73, 0x81 }, v3[] = { 0x01, 0x02 };
    uint32_t v;
    CHECK_EQ(mc_hmp_read_varlen(v1, 1, &v), 1); CHECK_EQ(v, 0);
    CHECK_EQ(mc_hmp_read_varlen(v2, 3, &v), 3); CHECK_EQ(v, 0x5f + (0x73 << 7) + (1 << 14));
    CHECK_EQ(mc_hmp_read_varlen(v3, 2, &v), 0);
    CHECK_EQ(mc_hmp_event_length(0x92, 0), 3); CHECK_EQ(mc_hmp_event_length(0xc1, 0), 2); CHECK_EQ(mc_hmp_event_length(0xa0, 0), 2);
    CHECK_EQ(mc_hmp_event_length(0xff, 0x2f), 3); CHECK_EQ(mc_hmp_event_length(0xff, 0x51), 5); CHECK_EQ(mc_hmp_event_length(0xff, 0x01), 2);
}

// ---- 3. sequencer -------------------------------------------------------------------------------------
struct RecMidi : MidiOut {
    struct Msg { uint32_t tick; uint8_t st, d1, d2; };
    std::vector<Msg> msgs;
    const MusicSequencer *seq = nullptr;
    void send(uint8_t st, uint8_t d1, uint8_t d2) override { msgs.push_back({seq ? seq->ticks_played() : 0, st, d1, d2}); }
};

static const BankTrack *find_track(int set, int dev, int index) {
    for (auto &t : g_tracks) if (t.set == set && t.dev == dev && t.index == index) return &t;
    return nullptr;
}

static void test_sequencer() {
    std::printf("== sequencer\n");
    const BankTrack *bt = find_track(0, 2, 1);
    CHECK(bt != nullptr);
    if (!bt) return;
    RecMidi rec;
    MusicSequencer seq;
    rec.seq = &seq;
    seq.set_output(&rec);
    seq.set_master_volume(0x40);
    CHECK_EQ(rec.msgs.size(), 16);
    for (auto &m : rec.msgs) CHECK_EQ(m.d2, (0x40 * 0x7f) >> 7);
    rec.msgs.clear();
    CHECK(seq.load_and_start(bt->bytes.data(), (uint32_t)bt->bytes.size()));
    CHECK(!seq.done());
    const mc_hmp_song &s = seq.song();
    // expected: every channel event with its parsed tick
    uint64_t want_events = 0, want_tick_sum = 0;
    std::vector<int> want_cc7;
    for (uint32_t t = 0; t < s.track_count; t++)
        for (uint32_t k = 0; k < s.tracks[t].event_count; k++) {
            const mc_hmp_event &e = s.tracks[t].events[k];
            if (e.status >= 0xf0) continue;
            want_events++; want_tick_sum += e.tick;
            if ((e.status & 0xf0) == 0xb0 && e.d1 == 7) want_cc7.push_back((0x40 * e.d2) >> 7);
        }
    uint32_t guard = 0;
    while (!seq.done() && guard++ < 1000000) seq.tick();
    CHECK_EQ(seq.ticks_played(), s.duration_ticks);
    int playing = 0;
    for (uint32_t t = 0; t < s.track_count; t++) playing += mc_hmp_track_plays(&s, (int)t, MC_HMP_DEVICE_MPU401);
    size_t reset_msgs = (size_t)playing * 4;
    CHECK(rec.msgs.size() == want_events + reset_msgs);
    uint64_t got_tick_sum = 0;
    std::vector<int> got_cc7;
    for (size_t i = 0; i + reset_msgs < rec.msgs.size(); i++) {
        got_tick_sum += rec.msgs[i].tick;
        if ((rec.msgs[i].st & 0xf0) == 0xb0 && rec.msgs[i].d1 == 7) got_cc7.push_back(rec.msgs[i].d2);
    }
    CHECK_EQ(got_tick_sum, want_tick_sum);
    CHECK(got_cc7 == want_cc7);
    // song end: per playing track 7B, 79, E0 40 40, 07 0 on the track's channel
    if (rec.msgs.size() >= reset_msgs && reset_msgs) {
        const RecMidi::Msg *r = &rec.msgs[rec.msgs.size() - reset_msgs];
        CHECK_EQ(r[0].st & 0xf0, 0xb0); CHECK_EQ(r[0].d1, 0x7b);
        CHECK_EQ(r[1].d1, 0x79); CHECK_EQ(r[2].st & 0xf0, 0xe0); CHECK_EQ(r[2].d1, 0x40); CHECK_EQ(r[2].d2, 0x40);
        CHECK_EQ(r[3].d1, 7); CHECK_EQ(r[3].d2, 0);
    }
    std::printf("  CGAME1.GEN: %u ticks (%.2f s), %zu messages, %d tracks, done %d\n", seq.ticks_played(),
                seq.ticks_played() / 120.0, rec.msgs.size(), playing, seq.done());
    // after the end the song is rewound but stays stopped until music_update restarts it
    size_t n = rec.msgs.size();
    seq.tick();
    CHECK_EQ(rec.msgs.size(), n);
    seq.set_layer_volume(0x22);
    CHECK_EQ(rec.msgs.size(), n + 3);
    CHECK_EQ(rec.msgs[n].st, 0xb3); CHECK_EQ(rec.msgs[n + 1].st, 0xb4); CHECK_EQ(rec.msgs[n + 2].st, 0xb5);
    CHECK_EQ(rec.msgs[n].d1, 7); CHECK_EQ(rec.msgs[n].d2, 0x22);
    // restart + stop mid-song: reset of the playing tracks + 16 x (79, 7B)
    CHECK(seq.load_and_start(bt->bytes.data(), (uint32_t)bt->bytes.size()));
    for (int i = 0; i < 500; i++) seq.tick();
    rec.msgs.clear();
    seq.stop();
    CHECK(seq.done()); CHECK(!seq.loaded());
    CHECK_EQ(rec.msgs.size(), reset_msgs + 32);
    CHECK(!seq.load_and_start(bt->bytes.data(), 100));                       // truncated: not an HMP

    // the game side: sound.cpp's music_play_track / music_update restart the song through AudioEngine
    AudioEngine e;
    e.set_output_rate(22050);
    RecMidi rec2;
    e.set_midi(&rec2, AudioEngine::CLOCK_RENDER);
    rec2.seq = &e.sequencer();
    sound_set_backend(&e);
    g_music_available = 1; g_music_on = 1; g_music_device = 2;
    music_stop();
    CHECK(music_load_bank(sim_game_dir(), 0));
    CHECK_EQ(g_music_track_count, 4);
    music_play_track(1);
    CHECK_EQ(g_music_track, 1); CHECK(!e.music_done());
    std::vector<int16_t> buf(2 * 1103);
    int restarts = 0;
    uint32_t last_ticks = 0;
    for (int step = 0; step < 20 * 270; step++) {                         // 270 s in game ticks of 50 ms
        music_update(1);
        e.render(buf.data(), 1102 + (step & 1));                           // 22050 / 20 = 1102.5 frames
        e.pump_fade_timers(50000);
        if (e.sequencer().ticks_played() < last_ticks) restarts++;
        last_ticks = e.sequencer().ticks_played();
    }
    std::printf("  through sound.cpp: track %d, %d restart(s) in 270 s, sequencer at tick %u\n", g_music_track, restarts, last_ticks);
    CHECK_EQ(restarts, 1);                                                  // 260 s song
    CHECK(last_ticks > 0 && last_ticks < 120 * 15);
    // combat mood: the layer ramps at 60 Hz through the fade timer
    rec2.msgs.clear();
    music_update(2);
    CHECK_EQ(e.timer_rate(MUSIC_TIMER_MOOD), 60);
    for (int step = 0; step < 30; step++) { e.render(buf.data(), 1102); e.pump_fade_timers(50000); }
    int layer_msgs = 0, last_layer = -1;
    for (auto &m : rec2.msgs) if (m.st >= 0xb3 && m.st <= 0xb5 && m.d1 == 7) { layer_msgs++; if (m.st == 0xb5) last_layer = m.d2; }
    std::printf("  combat layer: %d controller-7 messages on channels 3..5, level 0x%02x, timer %d Hz\n", layer_msgs, last_layer, e.timer_rate(MUSIC_TIMER_MOOD));
    CHECK_EQ(layer_msgs, 63 * 3);
    CHECK_EQ(last_layer, 0x7e);
    CHECK_EQ(e.timer_rate(MUSIC_TIMER_MOOD), 0);
    music_stop();
    CHECK_EQ(g_music_track, 0); CHECK(e.music_done());
    sound_set_backend(nullptr);
}

// ---- 4. offline renders ---------------------------------------------------------------------------------
struct Level { double peak = 0, rms = 0; size_t clipped = 0; };
static Level levels(const std::vector<int16_t> &pcm) {
    Level l;
    double sum = 0;
    for (int16_t s : pcm) { l.peak = (std::max)(l.peak, (double)std::abs((int)s)); sum += (double)s * s; l.clipped += s == 32767 || s == -32768; }
    l.rms = pcm.empty() ? 0 : std::sqrt(sum / (double)pcm.size());
    return l;
}

static void render_movie(const char *game_dir) {
    std::printf("== movie 0 from the snapshot, 60 s through AudioEngine (samples + square-synth music)\n");
    const int RATE = 44100, TICKS = 20 * 60;
    AudioEngine e;
    e.set_output_rate(RATE);
    SquareSynth synth;
    e.set_midi(&synth, AudioEngine::CLOCK_RENDER);
    sound_set_backend(&e);
    sound_reset();
    g_cfg->flags = 0; g_cfg->paused = 0;
    g_sound_available = 1; g_sound_on = 1; g_sound_quality = 1;
    g_music_available = 1; g_music_on = 1; g_music_device = 2;
    music_stop();
    CHECK(sound_load_bank(game_dir, 0));
    CHECK(music_load_bank(game_dir, 0));
    uint16_t saved_mode = g_video_mode_flags;
    sim_prepare_movie();
    if (!demo_open(game_dir, 0)) { std::printf("  movie 0 missing: SKIP\n"); g_video_mode_flags = saved_mode; sound_set_backend(nullptr); return; }
    std::vector<int16_t> pcm;
    pcm.reserve((size_t)RATE * 2 * 61);
    std::vector<int16_t> buf(2 * 2205);
    int peak_voices = 0;
    for (int tick = 0; tick < TICKS; tick++) {
        if (!demo_step()) break;                                         // sound_update / music_update run in the tick (sim_all hooks)
        if (tick == 0) music_start_level(38);                            // the snapshot's level
        e.pump_fade_timers(50000);
        e.render(buf.data(), RATE / 20);
        pcm.insert(pcm.end(), buf.begin(), buf.begin() + 2 * (RATE / 20));
        peak_voices = (std::max)(peak_voices, e.mixer().sounding());
    }
    g_video_mode_flags = saved_mode;
    demo_close();
    Level l = levels(pcm);
    std::printf("  %.1f s, %d voices started, peak %d voices sounding, music track %d (sequencer tick %u), peak %.0f, rms %.0f, %zu clipped samples\n",
                pcm.size() / 2.0 / RATE, e.voices_started(), peak_voices, g_music_track, e.sequencer().ticks_played(), l.peak, l.rms, l.clipped);
    CHECK(pcm.size() == (size_t)RATE * 2 * 60);
    CHECK(e.voices_started() > 50);
    CHECK(peak_voices <= AudioMixer::SLOTS);
    CHECK(l.rms > 200);
    CHECK_EQ(g_music_track, 1);
    if (!g_out.empty()) {
        CHECK(audio_write_wav(out_path("movie0.wav").c_str(), pcm.data(), pcm.size() / 2, 2, RATE));
        std::printf("  wrote %s\n", out_path("movie0.wav").c_str());
    }
    music_stop();
    sound_stop_all();
    sound_set_backend(nullptr);
}

static void render_tracks() {
    std::printf("== General MIDI tracks through the square-wave synth\n");
    const int RATE = 22050;
    for (auto &t : g_tracks) {
        if (t.dev != 2) continue;
        AudioEngine e;
        e.set_output_rate(RATE);
        SquareSynth synth;
        e.set_midi(&synth, AudioEngine::CLOCK_RENDER);
        CHECK(e.music_play(t.index, t.bytes.data(), (uint32_t)t.bytes.size()));
        std::vector<int16_t> pcm;
        std::vector<int16_t> buf(2 * 1024);
        int blocks = 0;
        while ((!e.music_done() || synth.notes_on()) && blocks < RATE / 1024 * 400) {
            e.render(buf.data(), 1024);
            pcm.insert(pcm.end(), buf.begin(), buf.end());
            blocks++;
        }
        Level l = levels(pcm);
        double secs = pcm.size() / 2.0 / RATE;
        std::printf("  music%d-2 %d %-12s %6.1f s  peak %5.0f  rms %5.0f\n", t.set, t.index, t.name.c_str(), secs, l.peak, l.rms);
        CHECK(e.music_done());
        CHECK(std::fabs(secs - e.sequencer().song().duration_ticks / 120.0) < 1.5 || e.sequencer().song().duration_ticks == 0);
        CHECK(l.rms > 100);
        if (!g_out.empty()) {
            char name[64];
            std::snprintf(name, sizeof name, "music%d-2-%d.wav", t.set, t.index);
            CHECK(audio_write_wav(out_path(name).c_str(), pcm.data(), pcm.size() / 2, 2, RATE));
        }
    }
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    g_out = argc > 2 ? argv[2] : "";
    test_mixer();
    test_hmp(game_dir);
    if (!sim_init(game_dir)) { std::printf("sim_init failed\n"); return 2; }
    sim_register_gameplay();
    test_sequencer();
    render_movie(game_dir);
    render_tracks();
    std::printf("%s: %d failure(s)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
