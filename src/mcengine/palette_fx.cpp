// Display palette, fades and in-game palette effects (round 5, task C). See palette_fx.h; report
// docs/analysis/port_fli.md.
//
// Translated: vga_palette_fade_61510 (both modes), vga_palette_fade_reset_61718,
// palette_effect_update_33010, palette_fade_out_and_load_32e40, mapmode_palette_save_30350,
// mapmode_palette_restore_303b0, title_screen_show_32db0. vga_read_palette_61ec8 / vga_set_palette_302f0
// are reads / writes of g_display_palette6.
#include "palette_fx.h"
#include "mc_globals.h"
#include "mcfile.h"
#include <cstring>
#include <string>

uint8_t g_display_palette6[768];
static bool s_dirty = true;

bool palette_display_dirty() { return s_dirty; }
void palette_display_clear_dirty() { s_dirty = false; }
void palette_display_set(const uint8_t *pal6) {          // vga_set_palette_302f0
    if (pal6 != g_display_palette6) std::memcpy(g_display_palette6, pal6, 768);
    s_dirty = true;
}

void (*g_hook_stereo_leave)() = nullptr;
void (*g_hook_stereo_enter)() = nullptr;

static std::string s_game_dir;
void palette_fx_set_game_dir(const char *game_dir) { s_game_dir = game_dir ? game_dir : ""; }

// ---------------------------------------------------------------------------------------------------
// vga_palette_fade_61510
// ---------------------------------------------------------------------------------------------------

static uint8_t  s_fade_start[768];      // DAT_0012ef10: the DAC when the fade began
static uint8_t  s_fade_black[768];      // DAT_0012f210: the zeroed default target
static uint8_t  s_fade_target[768];     // port: the blocking mode's target (the original reads the caller's buffer)
static int16_t  s_fade_k = 0;           // DAT_0012f510
static uint8_t  s_fade_steps = 0;       // the byte argument
static bool     s_fade_running = false; // port: a blocking fade is being stepped
static uint8_t  s_inc_active = 0;       // DAT_0009e860: incremental fade in progress

// The inner loop: entry = start + (int16)(target - start) * k / steps.
static void fade_compose(const uint8_t *target, int k, unsigned steps) {
    uint8_t out[768];
    for (int i = 0; i < 0x300; i++) {
        const int32_t diff = (int32_t)(int16_t)((int)target[i] - (int)s_fade_start[i]);
        const int32_t q = (diff * k) / (int32_t)steps;  // idiv
        out[i] = (uint8_t)(q + s_fade_start[i]);
    }
    palette_display_set(out);                           // vga_wait_vsync_65b10 + vga_set_palette_302f0
}

void palette_fade_start(const uint8_t *target6, int steps) {
    std::memcpy(s_fade_start, g_display_palette6, 768);  // vga_read_palette_61ec8
    if (target6) std::memcpy(s_fade_target, target6, 768);
    else { std::memset(s_fade_black, 0, 768); std::memcpy(s_fade_target, s_fade_black, 768); }
    s_fade_steps = (uint8_t)steps;
    s_fade_k = 0;
    s_fade_running = true;
    if (s_fade_steps == 0) {                             // port: the original would divide by zero
        palette_display_set(s_fade_target);
        s_fade_running = false;
        s_inc_active = 0;
    }
}

bool palette_fade_step() {
    if (!s_fade_running) return false;
    fade_compose(s_fade_target, s_fade_k, s_fade_steps);
    s_fade_k++;
    if (s_fade_k > (int16_t)s_fade_steps) {              // while (k <= steps)
        s_fade_running = false;
        s_inc_active = 0;                                // DAT_0009e860 = 0 after the blocking loop
        return false;
    }
    return true;
}

bool palette_fade_active() { return s_fade_running; }

int palette_fade_incremental(const uint8_t *target6, int steps) {
    const uint8_t st = (uint8_t)steps;
    if (s_inc_active == 0) {
        s_fade_k = 0;
        s_inc_active = 1;
        std::memcpy(s_fade_start, g_display_palette6, 768);
        if (!target6) std::memset(s_fade_black, 0, 768);
    } else {
        s_fade_k++;
        if ((uint16_t)st == (uint16_t)s_fade_k) s_inc_active = 0;
    }
    if (!target6) target6 = s_fade_black;
    if (st != 0) fade_compose(target6, s_fade_k, st);
    else palette_display_set(target6);                   // port: division by zero in the original
    return s_fade_k;
}

void palette_fade_reset() { s_inc_active = 0; }          // vga_palette_fade_reset_61718

// ---------------------------------------------------------------------------------------------------
// palette_effect_update_33010
// ---------------------------------------------------------------------------------------------------

static uint8_t s_effect_pal[768];       // DAT_000b6b80 (entry 0 is never written)
const uint8_t *palette_effect_buffer() { return s_effect_pal; }

static inline uint8_t clamp63(int v) { return (uint8_t)(v < 0 ? 0 : (v > 0x3f ? 0x3f : v)); }

// palette_fade_out_and_load_32e40
void palette_fade_out_and_load() {
    palette_fade_start(nullptr, 0x10);
    g_state->title_flag_a = 0;
    if (!s_game_dir.empty()) {
        char path[1024];
        mc_path_join(path, sizeof path, s_game_dir.c_str(), "data/palette.dat");
        mc_load_rnc_into(path, g_palette6, sizeof g_palette6);   // file_load_rnc_3cbe0 into [DAT_000adf90]
    }
}

void palette_effect_update() {
    Config *cfg = g_cfg;
    const uint8_t stage = cfg->fade_stage;
    if (stage < 2) {
        palette_fade_out_and_load();
        cfg->fade_stage++;
        return;
    }
    if (stage == 2) {
        cfg->palette_effect = 1;
        cfg->fade_stage = 3;
        return;
    }
    if (stage != 3) return;
    if (g_state->mode_3d != 0) palette_fade_reset();
    const uint8_t *src = g_palette6;                     // [DAT_000adf90]
    uint8_t *dst = s_effect_pal;
    switch (cfg->palette_effect) {
    case 1:
        if ((int16_t)palette_fade_incremental(g_palette6, 4) == 4) cfg->palette_effect = 0;
        return;
    case 2:
        for (int i = 3; i != 0x300; i += 3) { dst[i] = 0x3f; dst[i + 1] = src[i + 1]; dst[i + 2] = src[i + 2]; }
        break;
    case 3:
        for (int i = 3; i != 0x300; i += 3) {
            dst[i] = clamp63(src[i] + 0x30);
            dst[i + 1] = src[i + 1];
            dst[i + 2] = clamp63(src[i + 2] + 0x40);
        }
        break;
    case 4:
        for (int i = 3; i != 0x300; i += 3) { dst[i] = src[i]; dst[i + 1] = src[i + 1]; dst[i + 2] = 0x3f; }
        break;
    case 5:
        for (int i = 3; i != 0x300; i += 3) {
            dst[i] = clamp63(src[i + 2] - 0x20);         // (sic) the red channel is computed from blue
            dst[i + 1] = clamp63(src[i + 1] - 0x20);
            dst[i + 2] = clamp63(src[i + 2] - 0x20);
        }
        break;
    case 6:
        for (int i = 3; i != 0x300; i += 3) {
            dst[i] = clamp63(src[i + 2] + 0x30);         // (sic) blue again
            dst[i + 1] = clamp63(src[i + 1] + 0x20);
            dst[i + 2] = clamp63(src[i + 2] + 0x20);
        }
        break;
    case 7:
        for (int i = 3; i != 0x300; i += 3) {
            const uint8_t v = (uint8_t)(((int)src[i] + src[i + 1] + src[i + 2]) / 3);
            dst[i] = dst[i + 1] = dst[i + 2] = v;
        }
        break;
    default:                                             // 0 and > 7: nothing
        return;
    }
    palette_fade_reset();
    palette_display_set(s_effect_pal);
    cfg->palette_effect = 1;
}

// ---------------------------------------------------------------------------------------------------
// mapmode_palette_save_30350 / mapmode_palette_restore_303b0
// ---------------------------------------------------------------------------------------------------

static uint8_t s_saved_mode_3d = 0;     // DAT_00093fc0

void mapmode_palette_save() {
    if (g_state->mode_3d == 0) return;
    // (VFX1: when GameState+0x219e and the frame pitch is 0x280, vfx1_vip_stereo_leave_503d0 - not ported)
    s_saved_mode_3d = g_state->mode_3d;
    if (g_hook_stereo_leave) g_hook_stereo_leave();      // stereo_mode_leave_2ff10
    palette_display_set(g_palette6);
    g_state->mode_3d = 0;
}

void mapmode_palette_restore() {
    g_state->mode_3d = s_saved_mode_3d;
    if (s_saved_mode_3d == 1 && g_hook_stereo_enter) g_hook_stereo_enter();   // stereo_mode_enter_2ff50
    s_saved_mode_3d = 0;
}

// ---------------------------------------------------------------------------------------------------
// title_screen_show_32db0
// ---------------------------------------------------------------------------------------------------

bool title_screen_show(const char *game_dir, const FrameBuffer &fb) {
    if (!game_dir) return false;
    char path[1024];
    static uint8_t image[64000];
    mc_path_join(path, sizeof path, game_dir, "data/smatitle.dat");
    if (mc_load_rnc_into(path, image, sizeof image) <= 0) return false;
    uint8_t pal[768];
    mc_path_join(path, sizeof path, game_dir, "data/smatitle.pal");
    if (mc_load_rnc_into(path, pal, sizeof pal) <= 0) return false;
    // file_load_rnc_3cbe0 straight into the back buffer: a linear copy (the image's rows are 320 wide)
    if (fb.pixels) {
        const size_t n = (size_t)fb.width * fb.height;
        std::memcpy(fb.pixels, image, n < sizeof image ? n : sizeof image);
    }
    std::memcpy(g_palette6, pal, 768);
    palette_fade_start(g_palette6, 0x20);
    g_state->title_flag_a = 1;
    g_state->title_flag_b = 0;
    g_state->title_flag_c = 0;
    return true;
}
