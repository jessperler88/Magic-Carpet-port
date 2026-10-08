// FLI player of carpet.exe (round 5, task C). See fli.h; report docs/analysis/port_fli.md.
//
// Translated: fli_play_508f0 (as the frame-stepped FliPlayer), fli_read_frame_50520 (the original's
// "fli_error_unknown_frame"), movie_frame_present_50600 (decode / present step), fli_mem_read_504f0,
// fli_mem_decode_color256_509f0 (types 4 and 11), fli_mem_decode_ss2_50a90 (7),
// fli_mem_decode_lc_50be0 (12), fli_mem_decode_brun_50d00 (15), the BLACK / COPY / PSTAMP cases of
// 50600 (13 / 16 / 18), movie_wait_frame_50430 (pacing, fli_frame_due), cue_script_step_17d80, the
// subtitle strip movie_subtitle_init_23600 / movie_subtitle_show_236a0 / movie_buffer_clear_236d0 /
// movie_free_23700 / movie_subtitle_set_colour_23740, and the memory-stream chunk player
// flic_set_dest_50de0 / flic_play_chunk_50dfd / fli_decode_frame_50e88 / fli_read_file_header_50f36 /
// fli_skip_chunk_50f60 / fli_decode_ss2_50f71 / fli_decode_brun_51024.
#define _CRT_SECURE_NO_WARNINGS
#include "fli.h"
#include "sound.h"
#include "text.h"
#include "ui_draw.h"
#include "mc_globals.h"
#include "mcfile.h"
#include "sprite.h"
#include "gen/fli_tables.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------
// globals of the original
// ---------------------------------------------------------------------------------------------------

int32_t g_fli_frame_delay = 5;          // DAT_0009e704 (data segment value 5)
uint8_t g_fli_subtitles_enabled = 0;    // DAT_000938fc

static uint8_t  s_fli_palette[768];     // DAT_0012e798: the COLOR chunks' target (persists across movies)
static int16_t  s_cue_index = 0;        // DAT_000938f4
static int16_t  s_music_loop = 0;       // DAT_0012eaba: track + 1 of a 'Z' loop (0 = none)

// subtitle strip (movie_subtitle_*): DAT_0009390a initialised, DAT_000938fd shifted blit,
// DAT_0009390c current text, DAT_000adfd0 font (colour1 = DAT_000adfd6), strip = buffer + 0xe100,
// 0x4b00 bytes (DAT_000b2e04 / DAT_000b2e0c).
static bool          s_sub_init = false;
static bool          s_sub_shift = false;
static const char   *s_sub_text = nullptr;
static FontDesc      s_sub_font{};
static UiSpriteTable s_sub_table;
static bool          s_sub_font_loaded = false;
constexpr uint32_t   kSubStrip = 0xe100, kSubStripLen = 0x4b00, kSubShift = 0x1a40;

// ---------------------------------------------------------------------------------------------------
// cue scripts
// ---------------------------------------------------------------------------------------------------

#define CUE(f, tab, addr, ab, pal) { f, tab, (int)(sizeof tab / 7), addr, ab, pal }
static const FliCueScript s_cue_scripts[] = {
    // file                     records           exe addr  abort  palette   (caller)
    CUE("intro\\scroll.dat",   fli_cue_scroll,   0x516cc, false, false),   // 541f0 / 56e20 / 57580 (callback fe_fli_composite_bg_579c0)
    CUE("intro\\levelose.dat", fli_cue_levelose, 0x51710, true,  true),    // fe_screen_level_result_55b00 (status & 4)
    CUE("intro\\intel.dat",    fli_cue_intel,    0x51734, true,  true),    // fe_screen_intel_logo_56510 (Pentium only)
    CUE("intro\\intro.dat",    fli_cue_intro,    0x5174c, false, true),    // fe_screen_intro_movie_54900 (abort = DAT_0012ed35 & 2)
    CUE("intro\\title-01.dat", fli_cue_title01,  0x51ab8, true,  true),    // fe_screen_title_56730
    CUE("intro\\levelw1.dat",  fli_cue_levelwin, 0x51b28, true,  true),    // fe_screen_level_result_55b00 (status & 2, tick even)
    CUE("intro\\levelw2.dat",  fli_cue_levelwin, 0x51b28, true,  true),    // ... tick odd
    CUE("intro\\logo.dat",     fli_cue_logo,     0x51b70, true,  true),    // fe_screen_bullfrog_logo_563c0
    CUE("intro\\outro.dat",    fli_cue_outro,    0x51b88, false, true),    // fe_screen_outro_movie_54ab0
};
#undef CUE

static bool same_path(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca == '/') ca = '\\';
        if (cb == '/') cb = '\\';
        if (std::tolower(ca) != std::tolower(cb)) return false;
        if (ca == 0) return true;
    }
}

const FliCueScript *fli_cue_for_file(const char *rel_path) {
    if (!rel_path) return nullptr;
    for (const FliCueScript &s : s_cue_scripts)
        if (same_path(s.file, rel_path)) return &s;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------
// the player
// ---------------------------------------------------------------------------------------------------

struct FliPlayer {
    std::string game_dir;
    std::vector<uint8_t> file;          // whole FLIC (unpacked)
    uint16_t frames = 0, width = 0, height = 0;   // header (0x12eaa6 / a8 / aa)
    size_t   next_off = 12;             // DAT_0009e450: file position of the next frame chunk
    int16_t  frame = 0;                 // DAT_0012eac0: the frame about to be decoded
    int      current = -1;
    bool     ended = false;
    bool     aborted = false;           // DAT_0012eabc
    bool     cue_pending = true;        // the cue of `frame` has not run yet
    uint32_t shown_tick = 0;
    std::vector<uint8_t> image;         // decode target DAT_0009e444 (pitch = width) + subtitle strip
    std::vector<uint8_t> shown;         // port: `image` as the original blitted it, before the next cue ran
    bool     shown_shift = false;       // DAT_000938fd at that blit
    uint8_t  pal[768];
    const FliCueScript *cue = nullptr;
    bool     abort_on_input = true, apply_palette = true;
    uint32_t census[32] = {};
    int      unknown = 0;
};

static inline unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16); }

// fli_mem_read_504f0(dst, n): copy n bytes from the read pointer DAT_0012eab0 (or skip them when dst
// is null) and advance it. The port's reader is bounded: bytes behind the chunk read as 0.
struct MemReader {
    const uint8_t *data;
    uint32_t len, pos;
    void read(uint8_t *dst, uint32_t n) {
        if (dst)
            for (uint32_t i = 0; i < n; i++) dst[i] = (pos + i < len) ? data[pos + i] : 0;
        pos += n;
    }
    uint8_t  u8()  { uint8_t b = 0; read(&b, 1); return b; }
    uint16_t u16() { uint8_t b[2]; read(b, 2); return (uint16_t)rd16(b); }
};

// Bounded destination: the original writes wherever the data points; the port clips to w*h.
struct Dest {
    uint8_t *pix;
    uint32_t cap;
    void put(uint32_t at, uint8_t v) { if (at < cap) pix[at] = v; }
};

// fli_mem_decode_color256_509f0(kind): u16 packet count, per packet {u8 skip (entries), u8 count
// (0 = 256), count * 3 bytes}. The 0x100 / 0x40 argument (COLOR256 / COLOR64) is not used: the bytes
// go to the DAC buffer unscaled (the game's files carry 6-bit values in COLOR256 chunks).
static void decode_color(MemReader &r, uint8_t *pal) {
    const uint16_t packets = r.u16();
    uint32_t at = 0;                                    // ebx - 0x12e798
    for (uint16_t i = 0; i < packets; i++) {
        at += (uint32_t)r.u8() * 3;
        uint32_t count = r.u8();
        if (count == 0) count = 0x100;
        for (uint32_t k = 0; k < count; k++) {
            uint8_t rgb[3];
            r.read(rgb, 3);
            if (at + 3 <= 768) std::memcpy(pal + at, rgb, 3);   // port: the original runs past the buffer
            at += 3;
        }
    }
}

// fli_mem_decode_ss2_50a90: word-oriented delta. u16 line count; per line a word: 11xxxxxx = skip
// -word lines (not counted as a line), 10xxxxxx = low byte into the last pixel of the line (the
// original then ends the line - the FLC spec has a packet count follow), else the packet count;
// packets {u8 column skip, i8 n: n > 0 copy n words, n < 0 one word repeated -n times}.
static void decode_ss2(MemReader &r, Dest &d, uint32_t W) {
    const uint16_t lines = r.u16();
    uint32_t row = 0;                                   // ebp - dest
    for (uint16_t i = 0; i < lines; i++, row += W) {
        const uint16_t w = r.u16();
        uint32_t bx = row;
        if (w & 0x8000) {
            if (w & 0x4000) {
                const int32_t n = std::abs((int32_t)(int16_t)w);
                row += (uint32_t)(n - 1) * W;
                i--;                                    // the skip does not use up a line
            } else {
                d.put(row + W - 1, (uint8_t)w);
            }
            continue;
        }
        for (uint16_t pk = 0; pk < w; pk++) {
            bx += r.u8();
            const int8_t n = (int8_t)r.u8();
            if (n < 0) {
                uint8_t v[2];
                r.read(v, 2);
                for (int k = 0; k < -n; k++, bx += 2) { d.put(bx, v[0]); d.put(bx + 1, v[1]); }
            } else if (n > 0) {
                for (int k = 0; k < 2 * n; k++, bx++) d.put(bx, r.u8());
            }
        }
    }
}

// fli_mem_decode_lc_50be0: byte-oriented delta. u16 lines to skip, u16 line count; per line u8
// packets {u8 column skip, i8 n: n > 0 copy n bytes, n < 0 one byte repeated -n times}.
static void decode_lc(MemReader &r, Dest &d, uint32_t W) {
    uint32_t row = (uint32_t)r.u16() * W;
    const uint16_t lines = r.u16();
    for (uint16_t i = 0; i < lines; i++, row += W) {
        uint32_t bx = row;
        const unsigned packets = r.u8();
        for (unsigned pk = 0; pk < packets; pk++) {
            bx += r.u8();
            const int8_t n = (int8_t)r.u8();
            if (n < 0) {
                const uint8_t v = r.u8();
                for (int k = 0; k < -n; k++) d.put(bx++, v);
            } else if (n > 0) {
                for (int k = 0; k < n; k++) d.put(bx++, r.u8());
            }
        }
    }
}

// fli_mem_decode_brun_50d00: per line (height): u8 packet count (ignored), then until x >= width:
// i8 n: n < 0 copy -n bytes, n > 0 one byte repeated n times. n == 0 would loop forever in the
// original; the port ends the line.
static void decode_brun(MemReader &r, Dest &d, uint32_t W, uint32_t H) {
    for (uint32_t y = 0; y < (uint16_t)H; y++) {
        uint32_t bx = y * W;
        r.read(nullptr, 1);
        if ((uint16_t)W == 0) continue;
        uint16_t x = 0;
        do {
            const int8_t n = (int8_t)r.u8();
            int len;
            if (n < 0) {
                len = (n == -128) ? 128 : -n;           // (the original's byte abs makes -128 unusable)
                for (int k = 0; k < len; k++) d.put(bx + k, r.u8());
            } else if (n > 0) {
                len = n;
                const uint8_t v = r.u8();
                for (int k = 0; k < len; k++) d.put(bx + k, v);
            } else {
                break;
            }
            x = (uint16_t)(x + len);
            bx += len;
        } while (x < (uint16_t)W);
    }
}

// fli_read_frame_50520 + the decode half of movie_frame_present_50600 for the frame chunk at
// f->next_off. Returns false at the end of the file or for a chunk the original cannot read (it
// prints "unknown frame" / "frame too big" forever: magic != 0xF1FA, size >= 0xfa00).
static bool decode_next(FliPlayer *f, bool *pal_changed) {
    const size_t off = f->next_off;
    if (off + 16 > f->file.size()) return false;
    const uint8_t *h = f->file.data() + off;
    const uint32_t size = rd32(h);                      // DAT_0012e680
    const unsigned magic = rd16(h + 4);                 // DAT_0012e684
    const unsigned subchunks = rd16(h + 6);             // DAT_0012e686
    if (magic != 0xf1fa || size >= 0xfa00 || size < 16) return false;
    // (50600's 0xF100 prefix branch is unreachable: 50520 only returns for 0xF1FA.)
    const size_t avail = f->file.size() - (off + 16);
    MemReader r{f->file.data() + off + 16, (uint32_t)((size - 16) < avail ? (size - 16) : avail), 0};
    f->next_off = off + size;
    Dest d{f->image.data(), (uint32_t)f->width * f->height};
    const uint32_t W = f->width, H = f->height;
    for (unsigned k = 0; k < subchunks; k++) {
        const uint32_t start = r.pos;                   // ebp
        uint8_t hdr[6];
        r.read(hdr, 6);
        uint32_t csize = rd32(hdr);                     // [esp]
        const unsigned type = rd16(hdr + 4);
        f->census[type < 32 ? type : 0]++;
        switch ((uint16_t)(type - 4)) {
        case 0:  decode_color(r, f->pal); *pal_changed = true; break;          // 4  COLOR256 ("COLOUR256 ")
        case 3:  decode_ss2(r, d, W); break;                                    // 7  SS2
        case 7:  decode_color(r, f->pal); *pal_changed = true; break;          // 11 COLOR64
        case 8:  decode_lc(r, d, W); break;                                     // 12 LC
        case 9:  std::memset(f->image.data(), 0, (size_t)W * H); break;         // 13 BLACK
        case 11: decode_brun(r, d, W, H); break;                                // 15 BRUN
        case 12:                                                                // 16 COPY: the chunk size is replaced by
            r.read(f->image.data(), W * H);                                     //    w*h, so the next sub-chunk is read
            csize = W * H;                                                      //    6 bytes early (never in the files)
            break;
        case 14: break;                                                         // 18 PSTAMP: skipped
        default: f->unknown++; break;                                           // 5, 6, 8..10, 14, 17, others: skipped
        }
        r.pos = start + csize;
    }
    if (*pal_changed) std::memcpy(s_fli_palette, f->pal, 768);
    return true;
}

// ---------------------------------------------------------------------------------------------------
// subtitles (movie_subtitle_* 0x23600..0x23740)
// ---------------------------------------------------------------------------------------------------

// movie_buffer_clear_236d0
static void subtitle_clear(FliPlayer *f) {
    if (!s_sub_init || !f) return;
    if (f->image.size() >= kSubStrip + kSubStripLen) std::memset(f->image.data() + kSubStrip, 0, kSubStripLen);
}

// movie_subtitle_show_236a0(text): clear the strip, ui_draw_text_58ab0(0, 0, font, text) in the clip
// rect (10, 180, 300, 50), remember the text.
static void subtitle_show(FliPlayer *f, const char *text) {
    if (!s_sub_init) return;
    subtitle_clear(f);
    if (f && text && s_sub_font_loaded && f->width != 0) {
        const FrameBuffer saved_fb = g_ui_fb;
        const UiClipRect saved_clip = g_ui_clip;
        ui_set_target(FrameBuffer{f->image.data(), f->width, (int)(f->image.size() / f->width)});
        ui_set_clip_rect(10, 0xb4, 0x12c, 0x32);
        ui_fe_draw_text(0, 0, &s_sub_font, text);
        g_ui_clip = saved_clip;
        ui_set_target(saved_fb);
    }
    s_sub_text = text;
}

// movie_subtitle_init_23600: once: load data\screens\sfont1, font colour = nearest white of the
// palette DAT_000adf90, strip = buffer + 0xe100 (0x4b00 bytes), cleared; the blit is shifted.
static void subtitle_init(FliPlayer *f) {
    if (s_sub_init) return;
    s_sub_init = true;
    s_sub_shift = true;
    if (!s_sub_font_loaded && f) {
        if (mc_sprite_set_load(f->game_dir.c_str(), "data/screens/sfont1", &s_sub_table.set)) {
            const mc_sprite_set &set = s_sub_table.set;
            s_sub_table.entries.assign(set.count, UiSprite{nullptr, 0, 0, 0});
            for (size_t i = 0; i < set.count; i++) {
                const mc_tab_entry &e = set.entries[i];
                if (e.offset < set.dat.len)
                    s_sub_table.entries[i] = UiSprite{set.dat.data + e.offset, (uint32_t)(set.dat.len - e.offset), e.width, e.height};
            }
            s_sub_table.loaded = true;
            s_sub_font_loaded = true;
        }
    }
    if (s_sub_font_loaded) ui_font_init(&s_sub_font, &s_sub_table);
    s_sub_font.colour1 = (uint8_t)palette_find_nearest(g_palette6, 0x3f, 0x3f, 0x3f);
    subtitle_clear(f);
}

// movie_free_23700
static void subtitle_free(FliPlayer *f) {
    if (!s_sub_init) return;
    subtitle_clear(f);
    if (s_sub_font_loaded) {
        mc_sprite_set_free(&s_sub_table.set);
        s_sub_table.entries.clear();
        s_sub_table.loaded = false;
        s_sub_font_loaded = false;
    }
    s_sub_shift = false;
    s_sub_init = false;
    s_sub_text = nullptr;
}

// movie_subtitle_set_colour_23740
static void subtitle_set_colour(FliPlayer *f, uint8_t colour) {
    if (!s_sub_init) return;
    s_sub_font.colour1 = colour;
    if (s_sub_text) subtitle_show(f, s_sub_text);
}

const char *fli_subtitle_text() { return s_sub_init ? s_sub_text : nullptr; }
bool fli_subtitle_active() { return s_sub_shift; }
void fli_subtitle_free() { subtitle_free(nullptr); }

// ---------------------------------------------------------------------------------------------------
// cue_script_step_17d80(script): run every record whose frame equals the frame counter, then (when
// none is left for this frame) restart a 'Z' loop track whose song has ended.
// ---------------------------------------------------------------------------------------------------

static void cue_step(FliPlayer *f) {
    const FliCueScript *s = f->cue;
    if (!s) return;
    int16_t idx = s_cue_index;
    for (;;) {
        if (idx < 0 || idx >= s->count) break;          // port: the tables end with a never-matching record
        const uint8_t *rec = s->records + idx * 7;
        if ((uint16_t)f->frame != rd16(rec)) break;
        const int16_t arg = (int16_t)rd16(rec + 3);
        const uint8_t op = (uint8_t)(rec[2] - 0x41);
        switch (op <= 0x39 ? op : 0xff) {
        case 0x00: case 0x20:                           // 'A' frame delay
            g_fli_frame_delay = arg;
            break;
        case 0x01: case 0x21:                           // 'B' music bank
            music_stop();
            music_load_bank(f->game_dir.c_str(), (uint16_t)arg);
            break;
        case 0x04: case 0x24:                           // 'E' sample bank (speech sets 1..13)
            sound_stop_all();
            sound_load_bank(f->game_dir.c_str(), (uint8_t)arg);
            break;
        case 0x0a:                                      // 'K' clear the subtitle strip
            subtitle_clear(f);
            break;
        case 0x0b: case 0x2b:                           // 'L' (fli_stream_preload_5cb50 of the dead file
            // player when its buffer DAT_0009e844 exists - never), then as 'M'
        case 0x0c: case 0x2c:                           // 'M' music track
            music_play_track(arg);
            s_music_loop = 0;
            break;
        case 0x0e:                                      // 'O' subtitles on (only when enabled)
            if (g_fli_subtitles_enabled) subtitle_init(f);
            break;
        case 0x0f:                                      // 'P' subtitles off
            subtitle_free(f);
            break;
        case 0x10:                                      // 'Q' subtitle = text table entry arg
            subtitle_show(f, text_get(arg));
            break;
        case 0x11: case 0x31:                           // 'R' looping sample
            if (g_sound_available) sound_play_sample(0, arg, -1);
            break;
        case 0x12: case 0x32:                           // 'S' sample (0 = stop all)
            if (g_sound_available) {
                if (arg == 0) sound_stop_all();
                else sound_play_sample_loud(0, arg);
            }
            break;
        case 0x13: case 0x33:                           // 'T' stop sample (0 = stop all)
            if (g_sound_available) {
                if (arg == 0) sound_stop_all();
                else sound_stop_sample(0, arg);
            }
            break;
        case 0x17: case 0x37:                           // 'X' music off
            music_stop();
            s_music_loop = 0;
            break;
        case 0x19: case 0x39:                           // 'Z' looping music track
            music_play_track(arg);
            s_music_loop = (int16_t)(arg + 1);
            break;
        default:                                        // every other letter: nothing
            break;
        }
        idx++;
    }
    s_cue_index = idx;
    if (s_music_loop != 0 && music_song_done()) {
        music_stop();
        music_play_track((int16_t)(s_music_loop - 1));
    }
}

// ---------------------------------------------------------------------------------------------------
// contract
// ---------------------------------------------------------------------------------------------------

// fli_play_508f0's set-up: the 12-byte header {u32 size, u16 0xAF12, u16 frames, u16 w, u16 h} is
// read and the frames start at offset 12 whatever the size field says; the cue index (DAT_000938f4)
// and the music loop (DAT_0012eaba) are cleared.
static FliPlayer *open_bytes(const char *game_dir, const uint8_t *data, size_t len, const char *name) {
    if (!data || len < 12 || rd16(data + 4) != 0xaf12) return nullptr;
    FliPlayer *f = new FliPlayer;
    f->file.assign(data, data + len);
    f->frames = (uint16_t)rd16(data + 6);
    f->width  = (uint16_t)rd16(data + 8);
    f->height = (uint16_t)rd16(data + 10);
    if (f->width == 0 || f->height == 0 || f->width > 640 || f->height > 480) { delete f; return nullptr; }
    f->game_dir = game_dir ? game_dir : "";
    const size_t need = (size_t)f->width * f->height;
    f->image.assign(need + kSubShift > kSubStrip + kSubStripLen ? need + kSubShift : kSubStrip + kSubStripLen, 0);
    f->shown = f->image;
    std::memcpy(f->pal, s_fli_palette, 768);
    f->cue = fli_cue_for_file(name);
    if (f->cue) { f->abort_on_input = f->cue->abort_on_input; f->apply_palette = f->cue->apply_palette; }
    s_cue_index = 0;                                    // DAT_000938f4 = 0
    s_music_loop = 0;                                   // DAT_0012eaba = 0
    return f;
}

// Port: the movie override directory (fli.h fli_set_movie_dir).
static std::string s_movie_dir;
void fli_set_movie_dir(const char *dir) { s_movie_dir = dir ? dir : ""; }
const char *fli_movie_dir() { return s_movie_dir.c_str(); }

FliPlayer *fli_open(const char *game_dir, const char *rel_path) {
    if (!game_dir || !rel_path) return nullptr;
    char path[1024];
    mc_blob blob{};
    bool got = false;
    if (!s_movie_dir.empty()) {                         // port: <movie dir>/<file name>, e.g. the CD's INTRO folder
        const char *base = rel_path;
        for (const char *c = rel_path; *c; c++) if (*c == '\\' || *c == '/') base = c + 1;
        mc_path_join(path, sizeof path, s_movie_dir.c_str(), base);
        got = mc_read_unpacked(path, &blob) != 0;
    }
    if (!got) {
        mc_path_join(path, sizeof path, game_dir, rel_path);   // the original: file_open_619a0(DAT_0009e708)
        if (!mc_read_unpacked(path, &blob)) return nullptr;
    }
    FliPlayer *f = open_bytes(game_dir, blob.data, blob.len, rel_path);
    mc_blob_free(&blob);
    return f;
}

FliPlayer *fli_open_memory(const char *game_dir, const uint8_t *data, size_t len, const char *name) {
    return open_bytes(game_dir, data, len, name);
}

void fli_close(FliPlayer *f) { delete f; }
int fli_frame_count(const FliPlayer *f) { return f ? f->frames : 0; }
int fli_width(const FliPlayer *f) { return f ? f->width : 0; }
int fli_height(const FliPlayer *f) { return f ? f->height : 0; }
int fli_frame_delay_ticks(const FliPlayer *f) { return f ? g_fli_frame_delay : 0; }

// movie_wait_frame_50430: the frame is shown once DAT_0012eab4 (reset to 0 at every shown frame)
// reaches DAT_0009e704 (unsigned compare).
bool fli_frame_due(const FliPlayer *f, uint32_t now_ticks) {
    if (!f || f->ended) return false;
    if (f->current < 0 || f->aborted) return true;
    return (uint32_t)(now_ticks - f->shown_tick) >= (uint32_t)g_fli_frame_delay;
}

bool fli_next_frame(FliPlayer *f, uint32_t now_ticks, bool *palette_changed) {
    if (palette_changed) *palette_changed = false;
    if (!f || f->ended) return false;
    // while (DAT_0012eabc == 0 && frame < frames - 1)
    if ((int)f->frame >= (int)f->frames - 1) { f->ended = true; return false; }
    if (f->cue_pending) { cue_step(f); f->cue_pending = false; }
    bool pal = false;
    if (!decode_next(f, &pal)) { f->ended = true; return false; }
    f->current = f->frame;
    f->shown_tick = now_ticks;
    if (pal && f->apply_palette)                        // vga_set_palette + movie_subtitle_set_colour_23740
        subtitle_set_colour(f, (uint8_t)palette_find_nearest(f->pal, 0x3f, 0x3f, 0x3f));
    if (palette_changed) *palette_changed = pal;
    // The original blits here, before the next iteration's cue step: keep that image (the cue's
    // subtitle ops write into the decode buffer).
    f->shown = f->image;
    f->shown_shift = s_sub_shift;
    f->frame++;
    if (f->aborted || (int)f->frame >= (int)f->frames - 1) {
        f->ended = true;                                // the loop ends: no further cue step
    } else {
        cue_step(f);                                    // the next iteration's cue, right after the blit
    }
    return true;
}

int fli_current_frame(const FliPlayer *f) { return f ? f->current : -1; }
const uint8_t *fli_pixels(const FliPlayer *f) { return f ? f->shown.data() : nullptr; }
const uint8_t *fli_palette6(const FliPlayer *f) { return f ? f->pal : nullptr; }

// movie_frame_present_50600's blit: vga_copy_320x200_610f0 of the decode buffer, from byte 0x1a40 on
// while the subtitle strip is active (DAT_000938fd).
void fli_blit(const FliPlayer *f, const FrameBuffer &fb, int x, int y) {
    if (!f || !fb.pixels || f->current < 0) return;
    const size_t src0 = f->shown_shift ? kSubShift : 0;
    for (int row = 0; row < f->height; row++) {
        const int dy = y + row;
        if (dy < 0 || dy >= fb.height) continue;
        const uint8_t *src = f->shown.data() + src0 + (size_t)row * f->width;
        int x0 = 0, x1 = f->width;
        if (x + x0 < 0) x0 = -x;
        if (x + x1 > fb.width) x1 = fb.width - x;
        if (x1 <= x0) continue;
        std::memcpy(fb.pixels + (size_t)dy * fb.width + x + x0, src + x0, (size_t)(x1 - x0));
    }
}

void fli_abort(FliPlayer *f) { if (f) f->aborted = true; }
bool fli_abort_on_input(const FliPlayer *f) { return f ? f->abort_on_input : false; }
bool fli_apply_palette(const FliPlayer *f) { return f ? f->apply_palette : false; }
void fli_set_cue_script(FliPlayer *f, const FliCueScript *script) {
    if (!f) return;
    f->cue = script;
    if (script) { f->abort_on_input = script->abort_on_input; f->apply_palette = script->apply_palette; }
}
const uint32_t *fli_chunk_census(const FliPlayer *f) { return f ? f->census : nullptr; }
int fli_unknown_chunks(const FliPlayer *f) { return f ? f->unknown : 0; }

// ---------------------------------------------------------------------------------------------------
// memory-stream chunk player (0x50de0..0x510c9)
// ---------------------------------------------------------------------------------------------------

// flic_set_dest_50de0(width, height)
void fli_chunk_set_dest(FliChunkState *s, int width, int height) {
    if (!s) return;
    s->width = (uint16_t)width;
    s->height = (uint16_t)height;
}

namespace {
struct ChunkCtx {
    const uint8_t *data;
    size_t len;
    uint8_t *dest;
    size_t dest_len;
    const FliChunkState *s;
    bool ok = true;
    uint8_t byte(size_t at) { if (at < len) return data[at]; ok = false; return 0; }
    void put(size_t at, uint8_t v) { if (at < dest_len) dest[at] = v; }
    void put_t(size_t at, uint8_t v) { if (s->transparent != 1 || v != 0) put(at, v); }
};
}

// fli_decode_ss2_50f71: returns the read position after the chunk's data (the original continues
// from there, not from the size field).
static size_t chunk_ss2(ChunkCtx &c, size_t p) {
    const size_t W = c.s->width;
    unsigned lines = (unsigned)c.byte(p) | ((unsigned)c.byte(p + 1) << 8);   // [ebp-8]
    p += 2;
    size_t d = 0;                                       // edi - dest
    unsigned guard = 0;
    do {
        size_t line_start;
        unsigned op;
        for (;;) {
            op = (unsigned)c.byte(p) | ((unsigned)c.byte(p + 1) << 8);
            p += 2;
            line_start = d;                             // [ebp-0x10]
            if (!c.ok) return p;
            if (!(op & 0x8000)) break;
            if (op & 0x4000) { d += (size_t)(-(int32_t)(int16_t)op) * W; continue; }
            // last byte of the line; the same word then serves as the packet count (no new read)
            c.put_t(d + W - 1, (uint8_t)op);
            break;
        }
        for (unsigned packets = op & 0xffff; packets != 0 && c.ok; packets--) {
            d += c.byte(p);
            const int8_t n = (int8_t)c.byte(p + 1);
            p += 2;
            if (n > 0) {                                // movsw x n (no transparency)
                for (int k = 0; k < 2 * n; k++) c.put(d++, c.byte(p++));
            } else {                                    // lodsw / stosw, inc dl: 0 = 256 words
                const unsigned words = n == 0 ? 256u : (unsigned)(-(int)n);
                const uint8_t lo = c.byte(p), hi = c.byte(p + 1);
                p += 2;
                for (unsigned k = 0; k < words; k++) { c.put(d++, lo); c.put(d++, hi); }
            }
        }
        d = line_start + W;
        lines = (lines - 1) & 0xffff;
        if (++guard > 0x10000) break;
    } while (lines != 0 && c.ok);
    return p;
}

// fli_decode_brun_51024: rows = height, row length = width; ends at chunk start + size - 6.
static void chunk_brun(ChunkCtx &c, size_t p) {
    const size_t W = c.s->width;
    unsigned rows = c.s->height;                        // bx (0 = 65536)
    size_t d = 0;
    do {
        p++;                                            // packet count byte
        uint16_t cx = (uint16_t)W;
        unsigned guard = 0;
        do {
            const int8_t n = (int8_t)c.byte(p++);
            if (!c.ok) return;
            if (n <= 0) {                               // copy -n (0: 256) bytes
                const unsigned cnt = n == 0 ? 256u : (unsigned)(-(int)n);
                cx = (uint16_t)(cx - cnt);
                for (unsigned k = 0; k < cnt; k++) c.put_t(d++, c.byte(p++));
            } else {                                    // repeat one byte n times
                cx = (uint16_t)(cx - (unsigned)n);
                const uint8_t v = c.byte(p++);
                for (int k = 0; k < n; k++) c.put_t(d++, v);
            }
            if (++guard > 0x10000) return;
        } while (cx != 0);
        rows = (rows - 1) & 0xffff;
    } while (rows != 0 && c.ok);
}

// flic_play_chunk_50dfd(ptr, dest)
size_t fli_chunk_play(FliChunkState *s, const uint8_t *data, size_t len, size_t off, uint8_t *dest, size_t dest_len) {
    if (!s || !data) return 0;
    ChunkCtx c{data, len, dest, dest_len, s};
    size_t p = off;
    for (;;) {
        if (p + 6 > len) return 0;
        const unsigned magic = rd16(data + p + 4);      // size DAT_0009e462, magic DAT_0009e45c
        p += 6;
        if (magic == 0xaf12) {                          // fli_read_file_header_50f36: w / h after the frame count
            if (p + 6 > len) return 0;
            s->width = (uint16_t)rd16(data + p + 2);
            s->height = (uint16_t)rd16(data + p + 4);
            p += 6;
            continue;
        }
        if (magic != 0xf1fa) return 0;
        break;
    }
    if (p + 10 > len) return 0;
    unsigned chunks = rd16(data + p);                   // DAT_0009e466
    p += 2 + 8;
    while (chunks != 0) {                               // fli_decode_frame_50e88
        chunks--;
        if (p + 6 > len) return 0;
        const unsigned size16 = rd16(data + p);         // only the low word of the size is used
        const unsigned type = rd16(data + p + 4);
        p += 6;
        if (type == 7) {
            p = chunk_ss2(c, p);
        } else if (type == 15) {
            chunk_brun(c, p);
            p += size16 - 6;
        } else {
            p += size16 - 6;                            // fli_skip_chunk_50f60 (4 and every other type)
        }
        if (!c.ok || p > len) return 0;
    }
    return p;
}
