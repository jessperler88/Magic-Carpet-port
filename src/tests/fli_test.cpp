// FLI player, cue scripts and palette effects (port round 5, task C):
//  1. every FLIC the game plays through fli_play_508f0 (intro\*.dat) decoded frame by frame through the
//     fli.h contract: frame counts, sizes, chunk census, no unknown chunk, palette of frame 0 against
//     the file's COLOR256 chunk; every frame cross-checked against the independent translation of the
//     memory-stream player (fli_chunk_play, flic_play_chunk_50dfd) and the ring frame; PPMs of a few
//     frames next to the executable;
//  2. the memory-stream FLICs of the front end (data\screens\globe.dat, scroll.dat, timer.dat):
//     frames until the data ends, PPMs;
//  3. the cue scripts: the intro script (2944 frames on the CD) on a synthetic 3000-frame FLIC with a
//     recording sound backend: event timeline, every speech sample id inside the bank loaded at that
//     point, total running time; the level-result / logo scripts on their real files;
//  4. the stepped fader against the arithmetic of vga_palette_fade_61510, the incremental mode and
//     palette_effect_update_33010 for every Config.palette_effect value, the fade_stage machine,
//     mapmode save / restore, title_screen_show_32db0;
//  5. the subtitle strip (cue ops O / Q / P) with the English text table.
// argv[1] = game dir.
#define _CRT_SECURE_NO_WARNINGS
#include "fli.h"
#include "palette_fx.h"
#include "sound.h"
#include "sndbank.h"
#include "text.h"
#include "ui_draw.h"
#include "mc_globals.h"
#include "mcfile.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { \
    std::printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); g_fail++; } } while (0)

static std::string g_out;      // directory of the executable

static void write_ppm(const std::string &name, const uint8_t *pix, int w, int h, const uint8_t *pal6) {
    std::string path = g_out + "/" + name;
    FILE *fp = std::fopen(path.c_str(), "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (int i = 0; i < w * h; i++)
        for (int c = 0; c < 3; c++) { const uint8_t v = pal6[pix[i] * 3 + c] & 0x3f; rgb[(size_t)i * 3 + c] = (uint8_t)((v << 2) | (v >> 4)); }
    std::fwrite(rgb.data(), 1, rgb.size(), fp);
    std::fclose(fp);
}

static std::vector<uint8_t> read_raw(const std::string &game, const char *rel) {
    char path[1024];
    mc_path_join(path, sizeof path, game.c_str(), rel);
    mc_blob b;
    std::vector<uint8_t> v;
    if (mc_read_unpacked(path, &b)) { v.assign(b.data, b.data + b.len); mc_blob_free(&b); }
    return v;
}
static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

// The palette of the first frame chunk's first COLOR256 sub-chunk (a full 256-entry packet), or false.
static bool first_palette(const std::vector<uint8_t> &d, uint8_t *pal) {
    if (d.size() < 12 + 16 + 6 + 4 + 768) return false;
    const uint8_t *s = d.data() + 12 + 16;
    if (rd16(s + 4) != 4 || rd16(s + 6) != 1 || s[8] != 0 || s[9] != 0) return false;
    std::memcpy(pal, s + 10, 768);
    return true;
}

// ---- recording sound backend ----------------------------------------------------------------------------
struct CueBackend : SoundBackend {
    struct Ev { int frame; std::string what; };
    std::vector<Ev> ev;
    int frame = 0;
    const FliPlayer *player = nullptr;              // the frame label = the cue's frame: current + 1
    int starts = 0, music_plays = 0, music_stops = 0;
    int label() const { return player ? fli_current_frame(player) + 1 : frame; }
    int start(int owner, int sample, const uint8_t *, uint32_t len, int rate, int volume, int pan, int loop_count, int hmi_flags) override {
        char b[160];
        const char *name = sound_bank() ? mc_sndbank_name(sound_bank(), sample) : "?";
        std::snprintf(b, sizeof b, "sample set %d #%d %s (%u bytes @%d Hz, vol %d pan %d loop %d flags 0x%x, owner %d)",
                      sound_bank() ? sound_bank()->set : -1, sample, name ? name : "?", len, rate, volume, pan, loop_count, hmi_flags, owner);
        ev.push_back({label(), b});
        return starts++;
    }
    void stop(int) override {}
    void set_volume(int, int) override {}
    bool is_playing(int) override { return false; }
    bool music_play(int track, const uint8_t *, uint32_t len) override {
        char b[96];
        std::snprintf(b, sizeof b, "music track %d (%u bytes, bank set %d)", track, len, music_bank() ? music_bank()->set : -1);
        ev.push_back({label(), b});
        music_plays++;
        return true;
    }
    void music_stop() override { music_stops++; }
    bool music_done() override { return false; }
    void music_set_volume(int) override {}
    void music_set_layer_volume(int) override {}
    void music_timer(MusicTimer, int) override {}
};

// Plays `f` to its end with an ideal clock; returns the ticks used. Calls `per_frame` after every frame.
template <class Fn>
static uint32_t play_all(FliPlayer *f, Fn per_frame) {
    uint32_t now = 1000;
    const uint32_t t0 = now;
    for (int guard = 0; guard < 2000000; guard++) {
        if (!fli_frame_due(f, now)) {
            if (!fli_frame_due(f, now + 100000)) break;  // ended
            now++;
            continue;
        }
        bool pal = false;
        if (!fli_next_frame(f, now, &pal)) break;
        per_frame(pal);
    }
    return now - t0;
}

// ---- 1. the fli_play_508f0 files --------------------------------------------------------------------------
static void test_movie_files(const std::string &game) {
    std::printf("== intro\\*.dat through the contract (fli_play_508f0 + decoders 509f0 / 50a90 / 50be0 / 50d00)\n");
    static const char *files[] = {"intro\\intel.dat", "intro\\intro.dat", "intro\\levelose.dat", "intro\\levelw1.dat",
                                  "intro\\levelw2.dat", "intro\\logo.dat", "intro\\outro.dat", "intro\\scroll.dat",
                                  "intro\\title-01.dat", "intro\\title-02.dat"};
    g_sound_available = 0;                              // no cue sound here (section 3)
    for (const char *rel : files) {
        FliPlayer *f = fli_open(game.c_str(), rel);
        CHECK(f != nullptr);
        if (!f) continue;
        const std::vector<uint8_t> raw = read_raw(game, rel);
        const int W = fli_width(f), H = fli_height(f), N = fli_frame_count(f);
        CHECK_EQ(W, 320); CHECK_EQ(H, 200);
        uint8_t pal0[768];
        const bool has_pal = first_palette(raw, pal0);
        CHECK(has_pal);
        // the independent decoder over the same bytes
        FliChunkState cs;
        std::vector<uint8_t> ref((size_t)W * H, 0);
        size_t off = 0;
        int shown = 0, mismatches = 0, pal_frames = 0;
        std::string base = rel + 6;                     // strip "intro\"
        base = base.substr(0, base.size() - 4);
        const uint32_t ticks = play_all(f, [&](bool pal) {
            const int fr = fli_current_frame(f);
            CHECK_EQ(fr, shown);
            if (pal) pal_frames++;
            if (fr == 0) {
                CHECK(pal);
                if (has_pal) CHECK(std::memcmp(fli_palette6(f), pal0, 768) == 0);
            }
            off = fli_chunk_play(&cs, raw.data(), raw.size(), off, ref.data(), ref.size());
            CHECK(off != 0);
            if (std::memcmp(ref.data(), fli_pixels(f), ref.size()) != 0) mismatches++;
            if (fr == 0 || fr == N / 2 || fr == N - 2) {
                char name[96];
                std::snprintf(name, sizeof name, "fli_%s_%03d.ppm", base.c_str(), fr);
                write_ppm(name, fli_pixels(f), W, H, fli_palette6(f));
            }
            shown++;
        });
        CHECK_EQ(shown, N - 1);                         // fli_play shows frames 0 .. frames - 2
        CHECK_EQ(mismatches, 0);
        CHECK_EQ(fli_unknown_chunks(f), 0);
        // the two chunks fli_play never shows: frame N-1 and the ring frame (back to frame 0?)
        std::vector<uint8_t> frame0;
        {
            FliChunkState c2; std::vector<uint8_t> b((size_t)W * H, 0);
            fli_chunk_play(&c2, raw.data(), raw.size(), 0, b.data(), b.size());
            frame0 = b;
        }
        size_t o2 = off;
        int extra = 0;
        while (o2 != 0 && o2 < raw.size()) {
            o2 = fli_chunk_play(&cs, raw.data(), raw.size(), o2, ref.data(), ref.size());
            if (o2) extra++;
        }
        const bool ring_is_frame0 = std::memcmp(ref.data(), frame0.data(), frame0.size()) == 0;
        const uint32_t *cen = fli_chunk_census(f);
        std::printf("  %-20s %3d frames %dx%d, shown %3d, %d palette frames, %u ticks @ delay %d; chunks:", rel, N, W, H,
                    shown, pal_frames, ticks, fli_frame_delay_ticks(f));
        for (int t = 0; t < 32; t++) if (cen[t]) std::printf(" %d x%u", t, cen[t]);
        std::printf("; %d unshown chunks, last == frame 0: %s, cue %s\n", extra, ring_is_frame0 ? "yes" : "no",
                    fli_cue_for_file(rel) ? "yes" : "none");
        CHECK_EQ(extra, 2);
        CHECK(ring_is_frame0);
        fli_close(f);
    }
    // the blit with clipping
    FliPlayer *f = fli_open(game.c_str(), "intro/logo.dat");
    CHECK(f != nullptr);
    if (f) {
        bool p;
        CHECK(fli_frame_due(f, 0));
        CHECK(fli_next_frame(f, 0, &p));
        std::vector<uint8_t> fb(640 * 480, 0xee);
        fli_blit(f, FrameBuffer{fb.data(), 640, 480}, 600, 400);   // clipped to 40 x 80
        int written = 0;
        for (uint8_t v : fb) if (v != 0xee) written++;
        int expect = 0;
        for (int y = 0; y < 80; y++) for (int x = 0; x < 40; x++) if (fli_pixels(f)[y * 320 + x] != 0xee) expect++;
        CHECK_EQ(written, expect);
        CHECK_EQ(fb[400 * 640 + 600], fli_pixels(f)[0]);
        // pacing: the next frame waits for the delay of the logo script ('A' 10 at frame 0)
        CHECK_EQ(fli_frame_delay_ticks(f), 10);
        CHECK(!fli_frame_due(f, 9));
        CHECK(fli_frame_due(f, 10));
        // abort: due at once, one more frame, then the end
        fli_abort(f);
        CHECK(fli_frame_due(f, 1));
        CHECK(fli_next_frame(f, 1, &p));
        CHECK_EQ(fli_current_frame(f), 1);
        CHECK(!fli_next_frame(f, 100, &p));
        CHECK(!fli_frame_due(f, 1000));
        fli_close(f);
    }
    CHECK(fli_open(game.c_str(), "intro\\missing.dat") == nullptr);
    CHECK(fli_open(game.c_str(), "data\\palette.dat") == nullptr);
}

// ---- 2. memory-stream FLICs of the front end ----------------------------------------------------------------
static void test_screen_files(const std::string &game) {
    std::printf("== data\\screens FLICs through the memory-stream player (flic_play_chunk_50dfd)\n");
    static const char *files[] = {"data\\screens\\globe.dat", "data\\screens\\scroll.dat", "data\\screens\\timer.dat", "intro\\title-02.dat"};
    for (const char *rel : files) {
        const std::vector<uint8_t> raw = read_raw(game, rel);
        CHECK(!raw.empty());
        if (raw.empty()) continue;
        uint8_t pal[768] = {};
        const bool has_pal = first_palette(raw, pal);
        FliChunkState cs;
        std::vector<uint8_t> img(320 * 200, 0);
        size_t off = 0;
        int frames = 0;
        const int hdr_frames = (int)rd16(raw.data() + 6);
        std::string base(rel);
        base = base.substr(base.rfind('\\') + 1);
        base = base.substr(0, base.size() - 4);
        for (;;) {
            const size_t n = fli_chunk_play(&cs, raw.data(), raw.size(), off, img.data(), img.size());
            if (n == 0) break;
            if (frames == 0 || frames == hdr_frames / 2) {
                char name[96];
                std::snprintf(name, sizeof name, "flimem_%s_%03d.ppm", base.c_str(), frames);
                write_ppm(name, img.data(), 320, 200, pal);
            }
            off = n;
            frames++;
            if (off >= raw.size()) break;
        }
        std::printf("  %-24s header frames %3d, %3d frames decoded, %dx%d, ended at %zu / %zu, palette chunk %s\n", rel,
                    hdr_frames, frames, cs.width, cs.height, off, raw.size(), has_pal ? "yes" : "no");
        CHECK(frames >= hdr_frames);
        CHECK_EQ(cs.width, 320);
    }
}

// ---- 3. cue scripts ----------------------------------------------------------------------------------------
static std::vector<uint8_t> make_synthetic(int frames) {
    std::vector<uint8_t> d(12 + (size_t)(frames + 1) * 16, 0);
    d[0] = 12; d[4] = 0x12; d[5] = 0xaf;
    d[6] = (uint8_t)frames; d[7] = (uint8_t)(frames >> 8);
    d[8] = 0x40; d[9] = 0x01; d[10] = 200;
    for (int i = 0; i <= frames; i++) {
        uint8_t *c = d.data() + 12 + (size_t)i * 16;
        c[0] = 16; c[4] = 0xfa; c[5] = 0xf1;           // empty frame
    }
    return d;
}

static void test_cues(const std::string &game) {
    std::printf("== cue scripts (cue_script_step_17d80)\n");
    CueBackend be;
    sound_set_backend(&be);
    g_sound_available = 1; g_sound_on = 1; g_music_available = 1; g_music_on = 1;
    g_sound_quality = 1; g_music_device = 0;
    // every table record decodes to a known op
    static const char *names[] = {"intro\\scroll.dat", "intro\\levelose.dat", "intro\\intel.dat", "intro\\intro.dat",
                                  "intro\\title-01.dat", "intro\\levelw1.dat", "intro\\levelw2.dat", "intro\\logo.dat", "intro\\outro.dat"};
    for (const char *n : names) {
        const FliCueScript *s = fli_cue_for_file(n);
        CHECK(s != nullptr);
        if (!s) continue;
        int prev = -1;
        for (int i = 0; i + 1 < s->count; i++) {        // the last record is the look-ahead stopper
            const uint8_t *r = s->records + i * 7;
            CHECK(std::strchr("ABEKLMOPQRSTXZI", r[2]) != nullptr);
            CHECK((int)rd16(r) >= prev);
            prev = (int)rd16(r);
        }
        const uint8_t *last = s->records + (s->count - 1) * 7;
        CHECK((int)rd16(last) < prev || rd16(last) == 0xffff || (int)rd16(last) > 3000);
    }
    CHECK(fli_cue_for_file("INTRO/LOGO.DAT") == fli_cue_for_file("intro\\logo.dat"));
    CHECK(fli_cue_for_file("intro\\title-02.dat") == nullptr);

    // the intro script over 3000 synthetic frames
    const std::vector<uint8_t> syn = make_synthetic(3000);
    FliPlayer *f = fli_open_memory(game.c_str(), syn.data(), syn.size(), "synthetic");
    CHECK(f != nullptr);
    if (!f) return;
    fli_set_cue_script(f, fli_cue_for_file("intro\\intro.dat"));
    g_fli_subtitles_enabled = 0;
    int bad_ids = 0, frames = 0;
    const size_t ev0 = be.ev.size();
    be.player = f;
    const uint32_t ticks = play_all(f, [&](bool) { frames++; });
    // every 'S' / 'R' sample of the script must exist in the bank loaded before it (checked from the log)
    for (size_t i = ev0; i < be.ev.size(); i++) if (be.ev[i].what.find("#") != std::string::npos && be.ev[i].what.find("(null)") != std::string::npos) bad_ids++;
    std::printf("  intro script: %d frames shown, %u ticks = %.1f s at 119.06 Hz, %zu sound / music events, final delay %d\n",
                frames, ticks, ticks / 119.06, be.ev.size() - ev0, g_fli_frame_delay);
    for (size_t i = ev0; i < be.ev.size(); i++)
        if (i < ev0 + 12 || i + 4 >= be.ev.size()) std::printf("    frame %4d  %s\n", be.ev[i].frame, be.ev[i].what.c_str());
        else if (i == ev0 + 12) std::printf("    ...\n");
    std::printf("    music / looping events:");
    for (size_t i = ev0; i < be.ev.size(); i++)
        if (be.ev[i].what.find("music") == 0 || be.ev[i].what.find("loop -1") != std::string::npos)
            std::printf(" [f%d %s]", be.ev[i].frame, be.ev[i].what.substr(0, be.ev[i].what.find(" (")).c_str());
    std::printf("\n");
    CHECK_EQ(frames, 2999);
    CHECK_EQ(bad_ids, 0);
    CHECK_EQ(g_fli_frame_delay, 5);                     // the last 'A' at frame 2944
    CHECK(be.music_plays >= 3);
    be.player = nullptr;
    fli_close(f);
    // speech ids against the loaded banks: replay and check sound_sample_count at each 'S'
    {
        const FliCueScript *s = fli_cue_for_file("intro\\intro.dat");
        int bank = -1, checked = 0, missing = 0;
        mc_sndbank b{};
        for (int i = 0; i + 1 < s->count; i++) {
            const uint8_t *r = s->records + i * 7;
            const int arg = (int16_t)rd16(r + 3);
            if (r[2] == 'E') { if (bank >= 0) mc_sndbank_free(&b); bank = arg; mc_sndbank_load(game.c_str(), bank, 1, &b); }
            if ((r[2] == 'S' || r[2] == 'R' || r[2] == 'T') && arg != 0) {
                checked++;
                if (arg > mc_sndbank_count(&b)) { missing++; std::printf("    frame %d: %c %d beyond bank %d (%d samples)\n", rd16(r), r[2], arg, bank, mc_sndbank_count(&b)); }
            }
        }
        if (bank >= 0) mc_sndbank_free(&b);
        std::printf("  intro script: %d sample references, %d outside their bank\n", checked, missing);
        CHECK_EQ(missing, 0);
    }
    // the real level-result / logo movies with their scripts
    static const char *real[] = {"intro\\levelw1.dat", "intro\\levelose.dat", "intro\\logo.dat", "intro\\intel.dat", "intro\\title-01.dat", "intro\\outro.dat"};
    for (const char *rel : real) {
        FliPlayer *m = fli_open(game.c_str(), rel);
        CHECK(m != nullptr);
        if (!m) continue;
        const size_t e0 = be.ev.size();
        be.player = m;
        const uint32_t t = play_all(m, [&](bool) {});
        std::printf("  %-20s %u ticks (%.2f s), events:", rel, t, t / 119.06);
        for (size_t i = e0; i < be.ev.size(); i++) std::printf(" [f%d %s]", be.ev[i].frame, be.ev[i].what.substr(0, be.ev[i].what.find(" (")).c_str());
        std::printf("\n");
        CHECK(be.ev.size() > e0);
        fli_close(m);
    }
    be.player = nullptr;
    sound_set_backend(nullptr);
}

// ---- 4. palette ------------------------------------------------------------------------------------------
static void test_fader() {
    std::printf("== vga_palette_fade_61510 (stepped) and palette_effect_update_33010\n");
    uint8_t start[768], target[768];
    for (int i = 0; i < 768; i++) { start[i] = (uint8_t)(i % 64); target[i] = (uint8_t)((i * 7) % 64); }
    start[0] = 63; target[0] = 0;                       // -63: truncation toward zero (k=1: 63 - 3 = 60, floor would give 59)
    start[1] = 0; target[1] = 63;
    palette_display_set(start);
    palette_fade_start(target, 0x10);
    CHECK(palette_fade_active());
    int calls = 0, bad = 0;
    for (;;) {
        const int k = calls;
        const bool more = palette_fade_step();
        calls++;
        for (int i = 0; i < 768; i++) {
            const int diff = (int)target[i] - (int)start[i];
            const int expect = start[i] + (diff * k) / 16;
            if (g_display_palette6[i] != (uint8_t)expect) bad++;
        }
        if (k == 1) { CHECK_EQ(g_display_palette6[0], 60); CHECK_EQ(g_display_palette6[1], 3); }
        if (!more) break;
        if (calls > 100) break;
    }
    CHECK_EQ(calls, 17);                                // k = 0 .. 16
    CHECK_EQ(bad, 0);
    CHECK(!palette_fade_active());
    CHECK(std::memcmp(g_display_palette6, target, 768) == 0);
    // fade to black
    palette_fade_start(nullptr, 0x20);
    int n = 0;
    while (palette_fade_step()) n++;
    CHECK_EQ(n + 1, 0x21);
    for (int i = 0; i < 768; i++) if (g_display_palette6[i] != 0) { bad++; break; }
    CHECK_EQ(bad, 0);
    // dirty flag
    palette_display_clear_dirty();
    CHECK(!palette_display_dirty());
    palette_display_set(start);
    CHECK(palette_display_dirty());

    // incremental mode, 4 steps: k = 0, 1, 2, 3, 4 then a new fade starts at 0
    palette_fade_reset();
    palette_display_set(start);
    int ks[6];
    for (int i = 0; i < 6; i++) ks[i] = palette_fade_incremental(target, 4);
    CHECK_EQ(ks[0], 0); CHECK_EQ(ks[1], 1); CHECK_EQ(ks[4], 4); CHECK_EQ(ks[5], 0);
}

static void test_effects() {
    for (int i = 0; i < 768; i++) g_palette6[i] = (uint8_t)((i * 5 + 3) % 64);
    auto ent = [](int e, int c) { return (int)g_palette6[e * 3 + c]; };
    auto out = [](int e, int c) { return (int)palette_effect_buffer()[e * 3 + c]; };
    auto clamp = [](int v) { return v < 0 ? 0 : (v > 63 ? 63 : v); };
    g_cfg->fade_stage = 3;
    g_state->mode_3d = 0;
    for (int eff = 2; eff <= 7; eff++) {
        g_cfg->palette_effect = (uint8_t)eff;
        palette_display_set(g_palette6);
        palette_effect_update();
        CHECK_EQ(g_cfg->palette_effect, 1);
        CHECK(std::memcmp(g_display_palette6 + 3, palette_effect_buffer() + 3, 765) == 0);
        int bad = 0;
        for (int e = 1; e < 256; e++) {
            int r = ent(e, 0), g = ent(e, 1), b = ent(e, 2), er = r, eg = g, eb = b;
            switch (eff) {
            case 2: er = 63; break;
            case 3: er = clamp(r + 0x30); eb = clamp(b + 0x40); break;
            case 4: eb = 63; break;
            case 5: er = clamp(b - 0x20); eg = clamp(g - 0x20); eb = clamp(b - 0x20); break;
            case 6: er = clamp(b + 0x30); eg = clamp(g + 0x20); eb = clamp(b + 0x20); break;
            case 7: er = eg = eb = (r + g + b) / 3; break;
            }
            if (out(e, 0) != er || out(e, 1) != eg || out(e, 2) != eb) bad++;
        }
        CHECK_EQ(bad, 0);
        // then effect 1: five ticks of the incremental fade back to g_palette6
        int ticks = 0;
        while (g_cfg->palette_effect == 1 && ticks < 10) { palette_effect_update(); ticks++; }
        CHECK_EQ(ticks, 5);
        CHECK_EQ(g_cfg->palette_effect, 0);
        CHECK(std::memcmp(g_display_palette6, g_palette6, 768) == 0);
    }
    // effect 0 and 8: nothing
    palette_display_clear_dirty();
    g_cfg->palette_effect = 0; palette_effect_update();
    g_cfg->palette_effect = 8; palette_effect_update();
    CHECK(!palette_display_dirty());
    CHECK_EQ(g_cfg->palette_effect, 8);
    // stage machine: 0 and 1 fade out (16 steps, blocking in the original) and reload the palette,
    // 2 requests the fade in, 3 runs it
    g_cfg->fade_stage = 0;
    g_cfg->palette_effect = 0;
    g_state->title_flag_a = 7;
    palette_effect_update();
    CHECK_EQ(g_cfg->fade_stage, 1);
    CHECK(palette_fade_active());
    CHECK_EQ(g_state->title_flag_a, 0);
    while (palette_fade_step()) {}
    for (int i = 0; i < 768; i++) if (g_display_palette6[i]) { CHECK(false); break; }
    palette_effect_update(); while (palette_fade_step()) {}
    CHECK_EQ(g_cfg->fade_stage, 2);
    palette_effect_update();
    CHECK_EQ(g_cfg->fade_stage, 3);
    CHECK_EQ(g_cfg->palette_effect, 1);
    uint8_t pal_file[768];
    std::memcpy(pal_file, g_palette6, 768);             // data/palette.dat (reloaded by stage 0)
    int t = 0;
    while (g_cfg->palette_effect == 1 && t < 10) {
        palette_effect_update();
        if (t == 1) CHECK_EQ(g_display_palette6[0x2f * 3], (uint8_t)((pal_file[0x2f * 3] * 1) / 4));   // k = 1 of 4 from black
        t++;
    }
    CHECK_EQ(t, 5);
    CHECK(std::memcmp(g_display_palette6, pal_file, 768) == 0);
    std::printf("  effects 2..7 by construction, 1 = 5-tick return fade, stage machine 0 -> 3: done\n");
}

static void test_mapmode_and_title(const std::string &game) {
    for (int i = 0; i < 768; i++) g_palette6[i] = (uint8_t)(i & 63);
    int enter = 0, leave = 0;
    static int *pe, *pl;
    pe = &enter; pl = &leave;
    g_hook_stereo_enter = [] { (*pe)++; };
    g_hook_stereo_leave = [] { (*pl)++; };
    g_state->mode_3d = 0;
    mapmode_palette_save();                             // nothing when mode_3d == 0
    CHECK_EQ(leave, 0);
    mapmode_palette_restore();
    CHECK_EQ(g_state->mode_3d, 0);
    g_state->mode_3d = 1;
    std::memset(g_display_palette6, 9, 768);
    mapmode_palette_save();
    CHECK_EQ(leave, 1);
    CHECK_EQ(g_state->mode_3d, 0);
    CHECK(std::memcmp(g_display_palette6, g_palette6, 768) == 0);
    mapmode_palette_restore();
    CHECK_EQ(g_state->mode_3d, 1);
    CHECK_EQ(enter, 1);
    mapmode_palette_restore();                          // the saved value was forgotten
    CHECK_EQ(g_state->mode_3d, 0);
    g_state->mode_3d = 2;
    mapmode_palette_save(); mapmode_palette_restore();
    CHECK_EQ(g_state->mode_3d, 2);
    CHECK_EQ(enter, 1);                                 // SIRDS: no enter call
    g_state->mode_3d = 0;
    g_hook_stereo_enter = nullptr; g_hook_stereo_leave = nullptr;

    // title_screen_show_32db0
    std::vector<uint8_t> fb(320 * 200, 0);
    std::memset(g_display_palette6, 0, 768);
    CHECK(title_screen_show(game.c_str(), FrameBuffer{fb.data(), 320, 200}));
    CHECK_EQ(g_state->title_flag_a, 1);
    int steps = 1;
    while (palette_fade_step()) steps++;
    CHECK_EQ(steps, 0x21);
    CHECK(std::memcmp(g_display_palette6, g_palette6, 768) == 0);
    write_ppm("title_smatitle.ppm", fb.data(), 320, 200, g_display_palette6);
    int nonzero = 0;
    for (uint8_t v : fb) if (v) nonzero++;
    CHECK(nonzero > 1000);
    std::printf("  mapmode save / restore, title_screen_show (%d non-zero pixels, 33 fade frames): done\n", nonzero);
}

// ---- 5. subtitles ----------------------------------------------------------------------------------------
static void test_subtitles(const std::string &game) {
    std::printf("== subtitles (cue ops O / Q / P, movie_subtitle_* 0x23600..0x23740)\n");
    CHECK(text_load(game.c_str(), 0));
    g_fli_subtitles_enabled = 1;
    g_sound_available = 0; g_music_available = 0;
    // a 60-frame script: O at 0, Q 4 at 1, palette change at 2 (real frame 0 data), Q 16 at 10, P at 50
    static uint8_t rec[6 * 7] = {
        0, 0, 'O', 5, 0, 0, 0,
        1, 0, 'Q', 4, 0, 0, 0,
        10, 0, 'Q', 16, 0, 0, 0,
        12, 0, 'K', 0, 0, 0, 0,
        30, 0, 'P', 5, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0,
    };
    static const FliCueScript script{"test", rec, 6, 0, true, true};
    FliPlayer *f = fli_open(game.c_str(), "intro\\logo.dat");
    CHECK(f != nullptr);
    if (!f) return;
    fli_set_cue_script(f, &script);
    std::vector<uint8_t> fb(320 * 200, 0);
    int fr = 0;
    bool active_at_5 = false, gone_at_40 = false, text_at_5 = false, text_at_11 = false;
    play_all(f, [&](bool pal) {
        if (pal) palette_display_set(fli_palette6(f));
        fli_blit(f, FrameBuffer{fb.data(), 320, 200}, 0, 0);
        const int c = fli_current_frame(f);
        if (c == 5) {
            active_at_5 = fli_subtitle_active();
            text_at_5 = fli_subtitle_text() && std::strncmp(fli_subtitle_text(), "The people", 10) == 0;
            write_ppm("fli_subtitle_005.ppm", fb.data(), 320, 200, fli_palette6(f));
        }
        if (c == 5 || c == 11) {
            int strip = 0, low = 0;
            for (int i = 0xe100; i < 0xe100 + 0x4b00; i++) if (fli_pixels(f)[i]) strip++;
            for (int i = 159 * 320; i < 200 * 320; i++) if (fb[i]) low++;
            std::printf("  frame %d: %d strip pixels set, %d in the bottom 41 screen rows\n", c, strip, low);
        }
        if (c == 11) {
            text_at_11 = fli_subtitle_text() && std::strncmp(fli_subtitle_text(), "Do not", 6) == 0;
            write_ppm("fli_subtitle_011.ppm", fb.data(), 320, 200, fli_palette6(f));
        }
        if (c == 40) gone_at_40 = !fli_subtitle_active() && fli_subtitle_text() == nullptr;
        fr++;
    });
    CHECK(active_at_5);
    CHECK(text_at_5);
    CHECK(text_at_11);
    CHECK(gone_at_40);
    fli_close(f);
    // an intro aborted before its 'P': the front end frees the strip (movie_free_23700)
    f = fli_open(game.c_str(), "intro/logo.dat");
    if (f) {
        fli_set_cue_script(f, &script);
        bool p;
        fli_next_frame(f, 0, &p);
        CHECK(fli_subtitle_active());
        fli_close(f);
        fli_subtitle_free();
        CHECK(!fli_subtitle_active());
        CHECK(fli_subtitle_text() == nullptr);
    }
    g_fli_subtitles_enabled = 0;
    std::printf("  strip on at frame 0, text 4 / 16 drawn, off at 30: done (fli_subtitle_*.ppm)\n");
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const std::string game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    g_out = argv[0];
    const size_t slash = g_out.find_last_of("/\\");
    g_out = slash == std::string::npos ? "." : g_out.substr(0, slash);
    mc_globals_init();
    {
        std::string probe = game + "/intro/logo.dat";
        FILE *fp = std::fopen(probe.c_str(), "rb");
        if (!fp) { std::printf("SKIP: no game data at %s\n", game.c_str()); return 0; }
        std::fclose(fp);
    }
    palette_fx_set_game_dir(game.c_str());
    test_movie_files(game);
    test_screen_files(game);
    test_cues(game);
    test_fader();
    test_effects();
    test_mapmode_and_title(game);
    test_subtitles(game);
    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::printf("fli_test: all checks passed\n");
    return 0;
}
