// Sound manager test (port round 4, task F):
//  1. the sample bank format: data/snds0-<quality>.dat/.tab parsed, the first samples written as .wav
//     into the scratch directory (argv[2], optional) for a listen check;
//  2. construction tests from the disassembly: priority compare, positional volume / pan, request
//     modes, channel allocation and reuse, the fade-in / fade-out steppers, the music mood machine
//     and the level track choice;
//  3. level 38 replayed to the snapshot tick and the shipped movie played from the snapshot with a
//     recording backend that logs every started voice (tick, sound id, requesting thing, owner,
//     volume, pan); counts and the first 20 events are printed.
// argv[1] = game dir, argv[2] = directory for the .wav files (default: none written).
#include "sim.h"
#include "sound.h"
#include "sndbank.h"
#include "thing.h"
#include "player.h"
#include "demo.h"
#include "level_features.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { \
    std::printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); g_fail++; } } while (0)

// ---- a recording backend ------------------------------------------------------------------------------
// Voices live for len / rate seconds at TICKS_PER_SEC game ticks per second (loops until stopped); the
// tick count only models channel reuse, the original mixes in real time.
struct SoundEvent { int tick, sample, thing, owner, volume, pan, loop, flags; };
struct RecordingBackend : SoundBackend {
    static constexpr int TICKS_PER_SEC = 20;
    struct Voice { int remaining = 0; bool loop = false; bool active = false; int volume = 0; };
    std::vector<Voice> voices;
    std::vector<SoundEvent> events;
    int tick = 0;
    int last_thing[MC_SOUND_IDS] = {};          // thing index of the last sound_request per id (test wrapper)
    int starts = 0, stops = 0, volume_sets = 0;
    int music_plays = 0, music_stops = 0, layer_sets = 0, master_sets = 0;
    int music_track = 0; bool music_playing = false; int timer_rate[2] = {0, 0};
    int last_layer_volume = -1, last_master_volume = -1;

    int start(int owner, int sample, const uint8_t *data, uint32_t len, int rate, int volume, int pan,
              int loop_count, int hmi_flags) override {
        starts++;
        Voice v;
        v.active = true;
        v.loop = loop_count != 0;
        v.volume = volume;
        v.remaining = data && rate > 0 ? (int)((uint64_t)len * TICKS_PER_SEC / (uint64_t)rate) + 1 : 1;
        int h = -1;
        for (size_t i = 0; i < voices.size(); i++) if (!voices[i].active) { h = (int)i; break; }
        if (h < 0) { voices.push_back(v); h = (int)voices.size() - 1; } else voices[(size_t)h] = v;
        events.push_back({tick, sample, sample < MC_SOUND_IDS ? last_thing[sample] : -1, owner, volume, pan, loop_count, hmi_flags});
        return h;
    }
    void stop(int h) override { stops++; if (h >= 0 && (size_t)h < voices.size()) voices[(size_t)h].active = false; }
    void set_volume(int h, int volume) override { volume_sets++; if (h >= 0 && (size_t)h < voices.size()) voices[(size_t)h].volume = volume; }
    bool is_playing(int h) override { return h >= 0 && (size_t)h < voices.size() && voices[(size_t)h].active; }
    bool music_play(int track, const uint8_t *song, uint32_t len) override {
        music_plays++; music_track = track; music_playing = song != nullptr && len > 0; return music_playing;
    }
    void music_stop() override { music_stops++; music_playing = false; }
    bool music_done() override { return !music_playing; }
    void music_set_volume(int v) override { master_sets++; last_master_volume = v; }
    void music_set_layer_volume(int v) override { layer_sets++; last_layer_volume = v; }
    void music_timer(MusicTimer which, int rate) override { timer_rate[which] = rate; }

    void advance() {                 // one game tick of real time
        tick++;
        for (auto &v : voices) if (v.active && !v.loop && --v.remaining <= 0) v.active = false;
    }
    int active_voices() const { int n = 0; for (auto &v : voices) if (v.active) n++; return n; }
    void reset_log() { events.clear(); starts = stops = volume_sets = 0; }
};
static RecordingBackend g_rec;

static void request_hook(int thing, int player, int sound) {
    if (sound >= 0 && sound < MC_SOUND_IDS) g_rec.last_thing[sound] = thing;
    sound_request_play(thing, player, sound);
}

// ---- 1. bank format ---------------------------------------------------------------------------------------
static void test_bank(const char *game_dir, const char *wav_dir) {
    std::printf("== sample banks\n");
    for (int q : {0, 1, 3}) {
        mc_sndbank b;
        if (!mc_sndbank_load(game_dir, 0, q, &b)) { std::printf("FAIL: snds0-%d missing\n", q); g_fail++; continue; }
        std::printf("snds0-%d: %d samples, %zu data bytes, %d Hz\n", q, mc_sndbank_count(&b), b.dat.len, b.rate);
        CHECK(mc_sndbank_count(&b) == 45 || mc_sndbank_count(&b) == 46);
        CHECK(b.records[0].name[0] == 0 && b.records[0].unk1e == 0);
        for (int i = 1; i <= mc_sndbank_count(&b); i++) {
            uint32_t len;
            const uint8_t *s = mc_sndbank_sample(&b, i, &len);
            CHECK(s != nullptr);
            CHECK(b.records[i].length % 16 == 0 && b.records[i].unk1e == 0x5a);
            CHECK(s && s + len <= b.dat.data + b.dat.len);
        }
        if (q == 1) {
            uint32_t len;
            mc_sndbank_sample(&b, 1, &len);
            CHECK_EQ(len, 151312 - 16);                               // WAVES2-.RAW, 6.86 s at 22050 Hz
            CHECK(std::strcmp(mc_sndbank_name(&b, 1), "WAVES2-.RAW") == 0);
            CHECK(std::strcmp(mc_sndbank_name(&b, 3), "EXPLOD3.RAW") == 0);
            CHECK(std::strcmp(mc_sndbank_name(&b, 0x1f), "MARKET.RAW") == 0);
            CHECK(std::strcmp(mc_sndbank_name(&b, 0x2a), "RUBBER.RAW") == 0);
            for (int i = 1; i <= 8; i++) {
                const uint8_t *s = mc_sndbank_sample(&b, i, &len);
                // 8-bit unsigned PCM: the mean sits near 0x80
                long sum = 0; for (uint32_t k = 0; k < len; k++) sum += s[k];
                int mean = len ? (int)(sum / (long)len) : 0x80;
                std::printf("  %2d %-13s %7u bytes %5.2f s mean 0x%02x\n", i, mc_sndbank_name(&b, i), len, len / (double)b.rate, mean);
                CHECK(mean > 0x70 && mean < 0x90);
                if (wav_dir && *wav_dir && i <= 6) {
                    char path[1024];
                    std::snprintf(path, sizeof path, "%s/snds0-1_%02d_%s.wav", wav_dir, i, mc_sndbank_name(&b, i));
                    CHECK(mc_sndbank_write_wav(&b, i, b.rate, path));
                }
            }
        }
        mc_sndbank_free(&b);
    }
    // -0 holds the same sounds at half the rate: same names, about half the bytes of -1
    mc_sndbank b0, b1;
    if (mc_sndbank_load(game_dir, 0, 0, &b0) && mc_sndbank_load(game_dir, 0, 1, &b1)) {
        uint32_t l0, l1;
        mc_sndbank_sample(&b0, 3, &l0); mc_sndbank_sample(&b1, 3, &l1);
        CHECK(l1 > 0 && l0 * 2 >= l1 - 64 && l0 * 2 <= l1 + 64);
        CHECK(std::strcmp(mc_sndbank_name(&b0, 3), mc_sndbank_name(&b1, 3)) == 0);
        mc_sndbank_free(&b0); mc_sndbank_free(&b1);
    }
    // music banks share the container
    mc_sndbank m;
    if (mc_sndbank_load_named(game_dir, "music", 0, 0, 0, &m)) {
        std::printf("music0-0: %d tracks:", mc_sndbank_count(&m));
        for (int i = 1; i <= mc_sndbank_count(&m); i++) {
            uint32_t len; const uint8_t *s = mc_sndbank_sample(&m, i, &len);
            std::printf(" %s (%u bytes, \"%.4s\")", mc_sndbank_name(&m, i), len, s ? (const char *)s : "");
        }
        std::printf("\n");
        CHECK_EQ(mc_sndbank_count(&m), 4);                        // CGAME1..3 (the level tracks) + CSETUP (front end)
        mc_sndbank_free(&m);
    } else { std::printf("FAIL: music0-0 missing\n"); g_fail++; }
}

// ---- 2. construction tests --------------------------------------------------------------------------------
static void place(Thing *t, int x, int y, int z) { t->x = (uint16_t)x; t->y = (uint16_t)y; t->z = (int16_t)z; }

static void test_construction() {
    std::printf("== construction\n");
    // sound_priority_ok_49c20
    CHECK(sound_priority_ok(100, 100)); CHECK(sound_priority_ok(92, 100)); CHECK(!sound_priority_ok(91, 100));
    CHECK(sound_priority_ok(0x7fff, 0)); CHECK(sound_priority_ok(0, 8)); CHECK(!sound_priority_ok(0, 9));

    // positional volume / pan: a listener at yaw 0 (facing -y), sources around it
    CHECK(sim_load_level(38));
    sound_register_handlers();
    g_sound_available = 1; g_sound_on = 1; g_sound_quality = 1;
    Thing *me = thing_at(g_state->players[g_state->local_player].thing % MC_THING_SLOTS);
    place(me, 0x8000, 0x8000, 0x400); me->yaw = 0;
    const int S = (me == thing_at(1)) ? 2 : 1;   // any slot but the listener: only position / flags / owner are read
    Thing *src = thing_at((unsigned)S);
    uint32_t saved_flags = src->flags; uint16_t saved_owner = src->owner; Pos saved_pos = *thing_pos(src);
    src->flags &= ~0x80u; src->owner = 77;
    int vol, pan;
    // straight ahead, 1 cell: range 0x3000, vol = (0x3000 - 0x100) * 0x7fff / 0x3000, centred (dist <= 0x140)
    place(src, 0x8000, 0x8000 - 0x100, 0x400);
    CHECK(sound_locate(S, &vol, &pan)); CHECK_EQ(vol, 32084); CHECK_EQ(pan, 0x7fff);
    // 2 cells to the right (+x): diff 0x200 -> range 0x2400, vol = (0x2400 - 0x200) * 0x7fff / 0x2400, pan hard right
    place(src, 0x8000 + 0x200, 0x8000, 0x400);
    CHECK(sound_locate(S, &vol, &pan)); CHECK_EQ(vol, 30946); CHECK_EQ(pan, 0xffff);
    // 2 cells to the left: same volume, pan hard left
    place(src, 0x8000 - 0x200, 0x8000, 0x400);
    CHECK(sound_locate(S, &vol, &pan)); CHECK_EQ(vol, 30946); CHECK_EQ(pan, 0);
    // 16 cells behind: diff 0x400 -> range 0x1800, vol = (0x1800 - 0x1000) * 0x7fff / 0x1800, centred (d = 0)
    place(src, 0x8000, 0x8000 + 0x1000, 0x400);
    CHECK(sound_locate(S, &vol, &pan)); CHECK_EQ(vol, 10922); CHECK_EQ(pan, 0x7fff);
    // 45 degrees front-right at 4 cells: diff 0x100 -> range 0x2a00; dist = isqrt(2 * 0x400^2) = 0x5a8
    place(src, 0x8000 + 0x400, 0x8000 - 0x400, 0x400);
    CHECK(sound_locate(S, &vol, &pan)); CHECK_EQ(vol, (0x2a00 - 0x5a8) * 0x7fff / 0x2a00); CHECK_EQ(pan, 0x7fff + 0x100 * 64);
    // behind, 24 cells: range 0x1800 = dist -> volume 0 -> dropped; 23 cells still too quiet (< 0x200)
    place(src, 0x8000, 0x8000 + 0x1800, 0x400);
    CHECK(!sound_locate(S, &vol, &pan));
    place(src, 0x8000, 0x8000 + 0x1700, 0x400);
    CHECK(sound_locate(S, &vol, &pan)); CHECK_EQ(vol, 1365);   // (0x1800 - 0x1700) * 0x7fff / 0x1800 = 0x555 >= 0x200 -> audible
    // ahead, 47 cells: audible (vol = 0x100 * 0x7fff / 0x3000 = 0x2aa), 49 cells: beyond 0x9000000 dist^2
    place(src, 0x8000, 0x8000 - 0x2f00, 0x400);
    CHECK(sound_locate(S, &vol, &pan)); CHECK_EQ(vol, 0x100 * 0x7fff / 0x3000);
    place(src, 0x8000, 0x8000 - 0x3100, 0x400);
    CHECK(!sound_locate(S, &vol, &pan));
    // deleted thing (flags 0x80) and "no thing"
    src->flags |= 0x80; CHECK(!sound_locate(S, &vol, &pan)); src->flags &= ~0x80u;
    CHECK(sound_locate(0, &vol, &pan)); CHECK_EQ(vol, 0x7fff); CHECK_EQ(pan, 0x7fff);
    CHECK(sound_locate(-5, &vol, &pan)); CHECK_EQ(vol, 0x7fff);

    // request modes and the per-id slot
    sound_set_backend(&g_rec);
    g_rec.voices.clear(); g_rec.reset_log();
    sound_reset();
    mc_sndbank bank;
    CHECK(mc_sndbank_load(sim_game_dir(), 0, 1, &bank));
    sound_set_bank(&bank);
    CHECK_EQ(sound_sample_count(), 46);
    place(src, 0x8000 + 0x200, 0x8000, 0x400);       // 2 cells right: 30946 / 0xffff
    int local = g_state->local_player;
    sound_request(S, local, 3);                      // EXPLOD3: mode 1 (restart), owner = Thing.owner
    const SoundRequest *r = sound_request_slot(3);
    CHECK_EQ(r->mode, 1); CHECK_EQ(r->volume, 30946); CHECK_EQ(r->pan, 0xffff); CHECK_EQ(r->owner, 77);
    sound_request(S, local, 7);                      // SQUAWK: mode 3 (play if idle)
    CHECK_EQ(sound_request_slot(7)->mode, 3);
    sound_request(S, local, 4);                      // SELECTSP: local-player variant -> owner 0
    CHECK_EQ(sound_request_slot(4)->mode, 1); CHECK_EQ(sound_request_slot(4)->owner, 0);
    sound_request(S, local + 1, 4);                  // another player's select: dropped (slot untouched)
    CHECK_EQ(sound_request_slot(4)->owner, 0);
    sound_request(S, -1, 0xe);                       // player -1: owner from the thing
    CHECK_EQ(sound_request_slot(0xe)->mode, 1); CHECK_EQ(sound_request_slot(0xe)->owner, 77);
    sound_request(S, local + 1, 0xe);                // other player: dropped
    CHECK_EQ(sound_request_slot(0xe)->owner, 77);
    sound_request(S, local, 6);                      // NULL.RAW: nothing
    CHECK_EQ(sound_request_slot(6)->mode, 0);
    sound_request(S, local, 0x2e);                   // out of the jump table
    CHECK_EQ(sound_request_slot(0x2e)->mode, 0);
    // priority: a quieter request (more than 8 below) does not replace the slot, a louder one does
    place(src, 0x8000, 0x8000 + 0x1000, 0x400);      // 10922 centred
    sound_request(S, local, 3);
    CHECK_EQ(sound_request_slot(3)->volume, 30946); CHECK_EQ(sound_request_slot(3)->pan, 0xffff);
    place(src, 0x8000, 0x8000 - 0x100, 0x400);       // 32084
    sound_request(S, local, 3);
    CHECK_EQ(sound_request_slot(3)->volume, 32084); CHECK_EQ(sound_request_slot(3)->pan, 0x7fff);
    // the local-player loops: fade-in on a new looping channel at volume 0
    sound_request(S, local, 2);                      // WHB03985: target 0x46
    CHECK_EQ(g_rec.events.size(), 1); CHECK_EQ(g_rec.events[0].sample, 2); CHECK_EQ(g_rec.events[0].volume, 0);
    CHECK_EQ(g_rec.events[0].loop, -1); CHECK_EQ(g_rec.events[0].flags, 0x4300);
    CHECK_EQ(sound_channel(0)->fadein, 1); CHECK_EQ(sound_channel(0)->fadein_target, 0x46);
    sound_request(S, local + 1, 2);                  // not the local player: nothing
    CHECK_EQ(g_rec.events.size(), 1);
    g_sound_quality = 3; sound_request(S, local, 1); CHECK_EQ(g_rec.events.size(), 1);   // low-memory bank: no loops
    g_sound_quality = 1;
    sound_request(S, local, 5);                      // FIRE: target 0x78, allowed with quality 3 too
    CHECK_EQ(g_rec.events.size(), 2); CHECK_EQ(sound_channel(1)->fadein_target, 0x78);

    // sound_update plays the slots: restart (3), if idle (7), local select (4), 0xe; then clears the volumes
    g_cfg->paused = 0;
    sound_update();
    CHECK_EQ(g_rec.events.size(), 6);
    CHECK_EQ(sound_request_slot(3)->mode, 0); CHECK_EQ(sound_request_slot(3)->volume, 0); CHECK_EQ(sound_request_slot(3)->played, 2);
    bool saw3 = false, saw7 = false;
    for (size_t i = 2; i < g_rec.events.size(); i++) {
        if (g_rec.events[i].sample == 3) { saw3 = true; CHECK_EQ(g_rec.events[i].volume, 32084); CHECK_EQ(g_rec.events[i].flags, 0x300); CHECK_EQ(g_rec.events[i].loop, 0); }
        if (g_rec.events[i].sample == 7) saw7 = true;
    }
    CHECK(saw3 && saw7);
    CHECK_EQ(g_rec.active_voices(), 6);
    CHECK_EQ(sound_count_playing(), 6); CHECK_EQ(sound_playing_count(), 6);
    CHECK(!sound_sample_done(77, 3)); CHECK(sound_sample_done(77, 9));
    // the channel of EXPLOD3 keeps volume >> 8 (the original's unit mix)
    for (int ch = 0; ch < MC_SOUND_CHANNELS; ch++)
        if (sound_channel(ch)->sample == 3) CHECK_EQ(sound_channel(ch)->volume, 32084 >> 8);
    // play if idle: a second SQUAWK from the same owner while the first plays is dropped, from another owner not
    size_t n = g_rec.events.size();
    sound_request(S, local, 7); sound_update();
    CHECK_EQ(g_rec.events.size(), n);
    src->owner = 78; sound_request(S, local, 7); sound_update();
    CHECK_EQ(g_rec.events.size(), n + 1);
    // restart: a second EXPLOD3 stops the first voice and starts a new one (same voice count)
    int before = g_rec.active_voices(); int stops = g_rec.stops;
    src->owner = 77; sound_request(S, local, 3); sound_update();
    CHECK_EQ(g_rec.stops, stops + 1); CHECK_EQ(g_rec.active_voices(), before);

    // fade-in stepping: +0x800 per tick from 0 until (0x46 << 8) - 1
    const SoundChannel *c0 = sound_channel(0);
    CHECK_EQ(c0->volume, 0x800 * 4);                 // four sound_update calls so far
    for (int i = 0; i < 4; i++) sound_update_fadein();
    CHECK_EQ(c0->volume, 0x4000); CHECK_EQ(c0->fadein, 1);
    sound_update_fadein();
    CHECK_EQ(c0->volume, 0x45ff); CHECK_EQ(c0->fadein, 0);
    CHECK_EQ(g_rec.voices[(size_t)c0->handle].volume, 0x45ff);
    // fade-out: sound_fade from the local player: -0x800 per tick, stopped at <= 0x1000
    sound_fade(S, local + 1, 2); CHECK_EQ(c0->fadeout, 0);        // other player: nothing
    sound_fade(S, local, 9);     CHECK_EQ(c0->fadeout, 0);        // not a loop id
    sound_fade(S, local, 2);
    CHECK_EQ(c0->fadeout, 1); CHECK_EQ(c0->fadeout_volume, 0x45ff);
    for (int i = 0; i < 6; i++) sound_update_fadeout();
    CHECK_EQ(c0->volume, 0x45ff - 6 * 0x800); CHECK_EQ(c0->fadeout, 1);
    sound_update_fadeout();
    CHECK_EQ(c0->fadeout, 0); CHECK_EQ(c0->volume, 0); CHECK(!g_rec.voices[(size_t)c0->handle].active);
    // a fade-in request on a playing loop only re-targets it
    n = g_rec.events.size();
    sound_request(S, local, 5);
    CHECK_EQ(g_rec.events.size(), n); CHECK_EQ(sound_channel(1)->fadein, 1);
    // channel exhaustion: 32 voices, the 33rd start fails
    sound_reset(); g_rec.voices.clear(); g_rec.reset_log();
    for (int i = 0; i < 40; i++) sound_play_simple(100 + i, 3, 0x7fff, 0x7fff);
    CHECK_EQ(g_rec.active_voices(), 32); CHECK_EQ(sound_count_playing(), 32);
    CHECK_EQ(sound_start_sample(999, 3, 0x7fff, 0x7fff), 0);
    sound_stop_sample(105, 3);
    CHECK_EQ(sound_count_playing(), 31);
    CHECK_EQ(sound_start_sample(999, 3, 0x7fff, 0x7fff), 1);
    sound_set_sample_volume(999, 3, 0x40);
    for (int ch = 0; ch < MC_SOUND_CHANNELS; ch++)
        if (sound_channel(ch)->owner == 999) { CHECK_EQ(sound_channel(ch)->volume, 0x3fff); CHECK_EQ(g_rec.voices[(size_t)sound_channel(ch)->handle].volume, 0x3fff); }
    sound_stop_all();
    CHECK_EQ(sound_count_playing(), 0);
    // voices expire with time: EXPLOD3 is 30144 bytes = 1.37 s = 28 ticks
    sound_play_simple(5, 3, 0x7fff, 0x7fff);
    for (int i = 0; i < 27; i++) g_rec.advance();
    CHECK_EQ(sound_count_playing(), 1);
    g_rec.advance(); g_rec.advance();
    CHECK_EQ(sound_count_playing(), 0);
    // sound off: nothing is requested, fades are cleared
    g_sound_on = 0;
    sound_request(S, local, 3); CHECK_EQ(sound_request_slot(3)->mode, 0);
    g_sound_on = 1;
    // paused: the slots stay pending
    sound_request(S, local, 3); g_cfg->paused = 1; sound_update(); CHECK_EQ(sound_request_slot(3)->mode, 1);
    g_cfg->paused = 0; sound_update(); CHECK_EQ(sound_request_slot(3)->mode, 0);

    // music: level track choice and the mood machine
    CHECK_EQ(music_track_for_level(38), 1);
    for (int lv = 0; lv < 70; lv++) { int t = music_track_for_level(lv); CHECK(t >= 1 && t <= 3); }
    g_music_available = 1; g_music_on = 1; g_music_device = 0;
    CHECK(music_load_bank(sim_game_dir(), 0));
    CHECK_EQ(g_music_track_count, 4);
    music_start_level(38);
    CHECK_EQ(music_level_track(), 1); CHECK_EQ(g_music_track, 1); CHECK_EQ(g_rec.music_track, 1); CHECK_EQ(g_music_volume, 100);
    music_play_track(1);                             // same track: no restart
    CHECK_EQ(g_rec.music_plays, 1);
    MusicMoodState ms = music_mood_state();
    CHECK_EQ(ms.mood, 1); CHECK_EQ(ms.active, 0); CHECK_EQ(ms.dir, -2);
    music_update(1);                                 // already calm
    CHECK_EQ(music_fade_rate(MUSIC_TIMER_MOOD), 0);
    music_update(2);                                 // combat: ramp the layer up at 60 Hz
    CHECK_EQ(music_fade_rate(MUSIC_TIMER_MOOD), 60); CHECK_EQ(music_mood_state().dir, 2);
    for (int i = 0; i < 62; i++) music_fade_tick(MUSIC_TIMER_MOOD);
    CHECK_EQ(music_mood_state().level, 0x7c); CHECK_EQ(music_mood_state().active, 1);
    music_fade_tick(MUSIC_TIMER_MOOD);
    CHECK_EQ(music_mood_state().level, 0x7e); CHECK_EQ(music_mood_state().active, 0); CHECK_EQ(music_fade_rate(MUSIC_TIMER_MOOD), 0);
    CHECK_EQ(g_rec.last_layer_volume, 0x7e);
    music_update(2);                                 // unchanged
    CHECK_EQ(music_fade_rate(MUSIC_TIMER_MOOD), 0);
    music_update(1);                                 // calm: down at 20 Hz
    CHECK_EQ(music_fade_rate(MUSIC_TIMER_MOOD), 20);
    for (int i = 0; i < 20; i++) music_fade_tick(MUSIC_TIMER_MOOD);
    CHECK_EQ(music_mood_state().level, 0x7e - 40);
    music_update(2);                                 // back to combat mid-fade: 60 Hz up from here
    CHECK_EQ(music_fade_rate(MUSIC_TIMER_MOOD), 60); CHECK_EQ(music_mood_state().dir, 2);
    for (int i = 0; i < 20; i++) music_fade_tick(MUSIC_TIMER_MOOD);
    CHECK_EQ(music_mood_state().level, 0x7e); CHECK_EQ(music_mood_state().active, 0);
    // the song ends: restarted calm with the layer reset
    g_rec.music_playing = false;
    music_update(2);
    CHECK_EQ(g_rec.music_plays, 2); CHECK_EQ(music_mood_state().level, 0); CHECK_EQ(music_mood_state().mood, 1);
    // fade out to silence then stop
    music_fade_out_begin();
    CHECK_EQ(music_fade_rate(MUSIC_TIMER_FADE_OUT), 60);
    for (int i = 0; i < 126; i++) music_fade_tick(MUSIC_TIMER_FADE_OUT);
    CHECK_EQ(g_rec.last_master_volume, 1); CHECK_EQ(g_music_track, 1);
    music_fade_tick(MUSIC_TIMER_FADE_OUT);
    CHECK_EQ(g_music_track, 0); CHECK_EQ(g_rec.last_master_volume, 0x7f); CHECK_EQ(music_fade_rate(MUSIC_TIMER_FADE_OUT), 0);
    CHECK_EQ(g_rec.music_stops, 1);
    // input requests: F2 / pause plumbing
    CHECK(sound_handle_input_request(INPUT_REQ_MUSIC_PLAY, music_level_track()));
    CHECK_EQ(g_music_track, 1);
    CHECK(sound_handle_input_request(INPUT_REQ_MUSIC_STOP, 0));
    CHECK_EQ(g_music_track, 0);
    CHECK(!sound_handle_input_request(INPUT_REQ_TOGGLE_RESOLUTION, 0));
    music_update(2);                                 // no track: nothing
    CHECK_EQ(music_fade_rate(MUSIC_TIMER_MOOD), 0);

    src->flags = saved_flags; src->owner = saved_owner; *thing_pos(src) = saved_pos;
    sound_set_bank(nullptr);
    mc_sndbank_free(&bank);
}

// ---- 3. level 38 and the movie ----------------------------------------------------------------------------
static void print_events(const char *what, const RecordingBackend &rec, int n) {
    int per_id[MC_SOUND_IDS] = {};
    for (const auto &e : rec.events) if (e.sample >= 0 && e.sample < MC_SOUND_IDS) per_id[e.sample]++;
    std::printf("%s: %zu voices started, %d stopped, %d volume changes; by sound id:", what, rec.events.size(), rec.stops, rec.volume_sets);
    for (int i = 0; i < MC_SOUND_IDS; i++) if (per_id[i]) std::printf(" %d:%d", i, per_id[i]);
    std::printf("\n  first %d events (tick, id name, thing, owner, volume, pan, loop, flags):\n", n);
    for (size_t i = 0; i < rec.events.size() && (int)i < n; i++) {
        const SoundEvent &e = rec.events[i];
        std::printf("  %5d  %2d %-13s thing %3d owner %3d vol %5d pan %5d %s 0x%x\n", e.tick, e.sample,
                    sound_bank() ? mc_sndbank_name(sound_bank(), e.sample) : "", e.thing, e.owner, e.volume, e.pan,
                    e.loop ? "loop" : "once", e.flags);
    }
}

static void tick_sound() {
    // sound_update / music_update run inside game_tick_sim through g_hook_sound_update /
    // g_hook_music_update (sim_register_gameplay); only the platform's HMI timer events are modelled here.
    // the HMI timer events at 20 / 60 Hz, modelled at TICKS_PER_SEC ticks per second
    static int accum[2] = {0, 0};
    for (int w = 0; w < 2; w++) {
        int rate = music_fade_rate((MusicTimer)w);
        if (!rate) { accum[w] = 0; continue; }
        accum[w] += rate;
        while (accum[w] >= RecordingBackend::TICKS_PER_SEC && music_fade_rate((MusicTimer)w)) {
            accum[w] -= RecordingBackend::TICKS_PER_SEC;
            music_fade_tick((MusicTimer)w);
        }
    }
    g_rec.advance();
}

static void test_level38() {
    std::printf("== level 38 to the snapshot tick\n");
    void (*saved_input)() = g_hook_player_local_input;
    g_hook_player_local_input = nullptr;
    g_video_mode_flags = 1;                         // the recording ran in 320x200 (level_features.h castle_footprint)
    g_cfg->flags = 0; g_cfg->paused = 0;
    CHECK(sim_load_level(38));
    sound_reset(); g_rec.voices.clear(); g_rec.reset_log(); g_rec.tick = 0; g_rec.music_plays = g_rec.layer_sets = 0;
    g_sound_available = 1; g_sound_on = 1; g_music_available = 1; g_music_on = 1;
    CHECK(sound_load_bank(sim_game_dir(), 0));
    music_stop();
    music_start_level(38);
    int requests = 0;
    static int *req_counter; req_counter = &requests;
    g_hook_sound_request = [](int thing, int player, int sound) { (*req_counter)++; request_hook(thing, player, sound); };
    for (int guard = 0; g_state->players[0].tick < 389 && guard < 1000; guard++) { game_tick_sim(); tick_sound(); }
    g_state->commands[0].cmd = 0x1e; g_state->commands[0].arg = 1;
    game_tick_sim(); tick_sound();
    for (int guard = 0; g_state->players[0].tick < 412 && guard < 1000; guard++) { game_tick_sim(); tick_sound(); }
    std::printf("level 38, 412 ticks: %d sound requests, %d voices started, music track %d (%d plays), mood %d layer 0x%02x\n",
                requests, (int)g_rec.events.size(), g_music_track, g_rec.music_plays, music_mood_state().mood, music_mood_state().level);
    print_events("level 38", g_rec, 20);
    CHECK(requests > 0);
    CHECK(!g_rec.events.empty());
    for (const auto &e : g_rec.events) { CHECK(e.volume >= 0 && e.volume <= 0x7fff); CHECK(e.pan >= 0 && e.pan <= 0xffff); }
    CHECK(sound_count_playing() <= 32);
    g_hook_sound_request = sound_request_play;
    g_video_mode_flags = 8;
    g_hook_player_local_input = saved_input;
}

static void test_movie(const char *game_dir) {
    std::printf("== movie 0 from the snapshot\n");
    g_cfg->flags = 0; g_cfg->paused = 0;
    uint16_t saved_mode = g_video_mode_flags;
    sim_prepare_movie();
    if (!demo_open(game_dir, 0)) { std::printf("movie 0 missing, playback skipped\n"); g_video_mode_flags = saved_mode; return; }
    sound_reset(); g_rec.voices.clear(); g_rec.reset_log(); g_rec.tick = 0; g_rec.music_plays = g_rec.layer_sets = 0;
    music_stop();
    int requests = 0, dropped_requests = 0;
    static int *req_counter; req_counter = &requests;
    static int *drop_counter; drop_counter = &dropped_requests;
    g_hook_sound_request = [](int thing, int player, int sound) {
        (*req_counter)++;
        int v, p;
        if (!sound_locate(thing, &v, &p)) (*drop_counter)++;
        request_hook(thing, player, sound);
    };
    int fades = 0;
    static int *fade_counter; fade_counter = &fades;
    g_hook_sound_fade = [](int thing, int player, int sound) { (*fade_counter)++; sound_fade_player_sound(thing, player, sound); };
    int ticks = 0, max_voices = 0, combat_ticks = 0;
    bool more = true;
    while (more && ticks < 20000) {
        more = demo_step();
        ticks++;
        if (ticks == 1) music_start_level(38);                     // the first step loaded the snapshot (level 38): its music
        tick_sound();
        if (sound_count_playing() > max_voices) max_voices = sound_count_playing();
        if (music_mood_state().mood == 2) combat_ticks++;
    }
    g_video_mode_flags = saved_mode;
    std::printf("movie 0: %d ticks, %d requests (%d out of range / too quiet), %d fade requests, %d voices started, peak %d voices, "
                "music track %d, %d song (re)starts, %d layer-volume writes, %d ticks in combat mood\n",
                ticks, requests, dropped_requests, fades, (int)g_rec.events.size(), max_voices, g_music_track, g_rec.music_plays,
                g_rec.layer_sets, combat_ticks);
    print_events("movie 0", g_rec, 20);
    CHECK(requests > 0 && !g_rec.events.empty());
    CHECK(max_voices <= 32);
    for (const auto &e : g_rec.events) { CHECK(e.volume >= 0 && e.volume <= 0x7fff); CHECK(e.pan >= 0 && e.pan <= 0xffff); CHECK(e.sample >= 1 && e.sample <= sound_sample_count()); }
    g_hook_sound_request = sound_request_play;
    g_hook_sound_fade = sound_fade_player_sound;
    demo_close();
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    const char *wav_dir = argc > 2 ? argv[2] : "";
    test_bank(game_dir, wav_dir);
    if (!sim_init(game_dir)) { std::printf("sim_init failed\n"); return 2; }
    sim_register_gameplay();
    sound_set_backend(&g_rec);
    test_construction();
    test_level38();
    test_movie(game_dir);
    std::printf("%s: %d failure(s)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
