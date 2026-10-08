// 2D drawing layer of carpet.exe: sprite tables, the span blitter, rectangle fills, HUD text, the
// front-end font engine and the colour cube. See ui_draw.h for the coordinate conventions.
//
// Original addresses: ui_set_font_4abe0, ui_font_space_width_4abc0, ui_font_line_height_4abd0,
// ui_draw_text_4a9a0, ui_draw_text_background_4aa90, ui_text_width_4ab60, ui_draw_glyph_4d240,
// ui_draw_icon_4d0dd, ui_draw_icon_rows_4d1c8, ui_draw_sprite_60688 (= vga_fill_rect_606c0),
// vga_draw_sprite_spans_6070d (+ _lo_606f8), ui_fill_rect_blend_224e0, ui_fill_rect_blend2_22610,
// ui_draw_sprite_shaded_22760, ui_shade_rect_23310, ui_copy_block_23140, gfx_fill_rect_320_60590,
// gfx_fill_rect_640_60610, gfx_fill_rows_320_4ce83, gfx_fill_rows_640_4cea9, gfx_put_pixel_320_60f3c,
// gfx_put_pixel_640_60f7c, vga_draw_box_603f0, vga_draw_rect_outline_640_604c0,
// vga_fill_rect_clipped_6abbc, gfx_fill_rect_clipped_6acd4, palette_find_nearest_60fc0,
// data_load_all_334c0 (colour cube), ui_sprite_lists_relocate_49de0, sprite_table_relocate_62ad0,
// sprite_table_relocate_x2_62a80, ui_set_clip_rect_588b0, ui_get_clip_rect_588f0,
// ui_push_clip_rect_58930, ui_pop_clip_rect_58950, ui_text_width_58970, ui_font_init_589d0,
// ui_draw_text_58ab0, ui_draw_glyph_58ba0, ui_font_relocate_glyphs_58f30, debug_screenshot_3ca00.
#define _CRT_SECURE_NO_WARNINGS
#include "ui_draw.h"
#include "mc_globals.h"
#include "mcfile.h"
#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------

UiSpriteTable g_hud_sprites;          // DAT_000adf94
UiSpriteTable g_ui_font_tables[2];    // DAT_000adf28, DAT_000adf2c
UiSpriteTable g_ui_pointers;          // DAT_000adfc0
UiSpriteTable g_ui_building;          // DAT_000adfb0
const UiSpriteTable *g_ui_font = &g_ui_font_tables[0];   // DAT_000adfbc
uint8_t   g_colour_cube[4096];        // DAT_000acc18
uint16_t  g_ui_blit_flags = 0;        // DAT_0009e5c4
FrameBuffer g_ui_fb{nullptr, 0, 0};   // DAT_0012ed74 / ed70 / ed78
UiClipRect g_ui_clip{0, 0, 0, 0, 0, 0};   // DAT_0012ead0..eae4
static UiClipRect s_clip_saved;           // DAT_0012eae8 (ui_push_clip_rect_58930)
static std::string s_game_dir;
static bool s_hud_lo_res = false;     // which HUD set is loaded (mspr0-0 when true)

static const UiSprite s_null_sprite{nullptr, 0, 0, 0};

const UiSprite *ui_sprite(const UiSpriteTable &t, unsigned index) {
    if (!t.loaded || index >= t.entries.size()) return &s_null_sprite;
    return &t.entries[index];
}

void ui_set_target(const FrameBuffer &fb) { g_ui_fb = fb; }

// ---------------------------------------------------------------------------------------------
// loading / relocation
// ---------------------------------------------------------------------------------------------

// sprite_table_relocate_62ad0 / sprite_table_relocate_x2_62a80: the original adds the .dat base to
// every offset once (and doubles the size bytes in the 320x200 mode). The port keeps the raw
// offsets in `set` and rebuilds the pointer table, so it can be redone after a mode switch.
static void relocate_table(UiSpriteTable &t) {
    t.entries.clear();
    if (!t.loaded) return;
    const bool x2 = ui_lo_res();
    t.entries.resize(t.set.count);
    for (size_t i = 0; i < t.set.count; i++) {
        const mc_tab_entry &e = t.set.entries[i];
        UiSprite &s = t.entries[i];
        if (e.offset < t.set.dat.len) {
            s.data = t.set.dat.data + e.offset;
            s.size = (uint32_t)(t.set.dat.len - e.offset);
        } else {
            s.data = nullptr; s.size = 0;
        }
        s.w = (uint8_t)(x2 ? e.width << 1 : e.width);      // `shl byte ptr [eax+4], 1` wraps at 256 like this
        s.h = (uint8_t)(x2 ? e.height << 1 : e.height);
    }
}

// ui_sprite_lists_relocate_49de0 over the tab list 0x9744c (pointers, font0, font1, HUD, building).
void ui_sprite_lists_relocate() {
    relocate_table(g_ui_pointers);
    relocate_table(g_ui_font_tables[0]);
    relocate_table(g_ui_font_tables[1]);
    relocate_table(g_hud_sprites);
    relocate_table(g_ui_building);
}

static bool load_table(UiSpriteTable &t, const char *game_dir, const char *name) {
    if (t.loaded) { mc_sprite_set_free(&t.set); t.loaded = false; t.entries.clear(); }
    if (!mc_sprite_set_load(game_dir, name, &t.set)) return false;
    t.loaded = true;
    return true;
}

static bool load_hud_table(const char *game_dir) {
    s_hud_lo_res = ui_lo_res();
    // resource lists 0x97294 (320x200: data/mspr0-0) / 0x97370 (640x480: data/hspr0-0), video_alloc_buffers_33480
    return load_table(g_hud_sprites, game_dir, s_hud_lo_res ? "data/mspr0-0" : "data/hspr0-0");
}

bool ui_draw_init(const char *game_dir) {
    s_game_dir = game_dir;
    bool ok = load_table(g_ui_font_tables[0], game_dir, "data/font0");
    ok &= load_table(g_ui_font_tables[1], game_dir, "data/font1");
    load_table(g_ui_pointers, game_dir, "data/pointers");
    load_table(g_ui_building, game_dir, "data/building");
    ok &= load_hud_table(game_dir);
    ui_sprite_lists_relocate();
    ui_colour_cube_build(g_palette6);          // "Initialise Colour Lookup" of data_load_all_334c0
    ui_set_font(0);
    g_ui_blit_flags = 0;
    return ok;
}

void ui_draw_shutdown() {
    UiSpriteTable *tabs[] = {&g_hud_sprites, &g_ui_font_tables[0], &g_ui_font_tables[1], &g_ui_pointers, &g_ui_building};
    for (UiSpriteTable *t : tabs) {
        if (t->loaded) mc_sprite_set_free(&t->set);
        t->loaded = false;
        t->entries.clear();
    }
}

bool ui_draw_reload_set_tables() {
    const char *dir = s_game_dir.c_str();
    load_table(g_ui_building, dir, "data/building");
    const bool ok = load_hud_table(dir);
    ui_sprite_lists_relocate();
    ui_colour_cube_build(g_palette6);
    return ok;
}

bool ui_draw_set_video_mode(const char *game_dir) {
    if (game_dir) s_game_dir = game_dir;
    bool ok = true;
    if (!g_hud_sprites.loaded || s_hud_lo_res != ui_lo_res()) ok = load_hud_table(s_game_dir.c_str());
    ui_sprite_lists_relocate();
    return ok;
}

// ---------------------------------------------------------------------------------------------
// colour cube
// ---------------------------------------------------------------------------------------------

// palette_find_nearest_60fc0(pal, r, g, b)
int palette_find_nearest(const uint8_t *pal6, int r, int g, int b) {
    // 0x10 entries when DAT_0012edae & 6 (16-colour modes, never set here), else 0x100
    const int count = (g_video_mode_flags & 6) ? 0x10 : 0x100;
    int best = 0x270f;          // 9999
    int best_i = 0;             // the original leaves this uninitialised when nothing beats 9999
    for (int i = 0; i < count; i++) {
        const int dg = (g & 0xff) - pal6[i * 3 + 1];
        const int dr = (r & 0xff) - pal6[i * 3 + 0];
        const int db = (b & 0xff) - pal6[i * 3 + 2];
        const int d = dg * dg + dr * dr + db * db;
        if ((int16_t)d < (int16_t)best) { best = d; best_i = i; }     // `cmp ax, word [best]` (16-bit)
    }
    return best_i;
}

// data_load_all_334c0, "Initialise Colour Lookup": component c of the cube is the 6-bit value c * 4 + 3
// (0x334f3 / 0x3350e / 0x3352c: `shl al, 2; add al, 3`), i.e. the centre of each 4-step bin - so cube[0]
// is palette entry 12 (4, 3, 3), not 0 (0, 0, 0). Fixed in round 5 (render reference, port_render_reference.md).
void ui_colour_cube_build(const uint8_t *pal6) {
    for (int r = 0; r < 16; r++)
        for (int g = 0; g < 16; g++)
            for (int b = 0; b < 16; b++)
                g_colour_cube[(r << 8) | (g << 4) | b] =
                    (uint8_t)palette_find_nearest(pal6, r * 4 + 3, g * 4 + 3, b * 4 + 3);
}

// ---------------------------------------------------------------------------------------------
// span blitter
// ---------------------------------------------------------------------------------------------

enum BlitMode { BLIT_COPY, BLIT_SOLID, BLIT_BLEND, BLIT_TINT, BLIT_SHADE };

// The common core of vga_draw_sprite_spans_6070d (modes 0 and 0x40) and of the C blitters
// ui_fill_rect_blend_224e0 / ui_fill_rect_blend2_22610 / ui_draw_sprite_shaded_22760: walks the
// span rows of `s` at the 640-space position (x, y), halving position and size in the 320x200 mode.
// `first_row` rows of the source are skipped (ui_draw_icon_rows_4d1c8). Pixels outside the target
// are dropped (the original clips sprites to the screen; its C blitters do not clip at all).
static void blit_spans(int x, int y, const UiSprite *s, BlitMode mode, int arg, int first_row = 0) {
    if (!s || !s->data || !g_ui_fb.pixels || s->h == 0) return;
    const bool lo = ui_lo_res();
    int w = s->w, h = s->h;
    if (lo) { x >>= 1; y >>= 1; w >>= 1; h >>= 1; }       // sar ebx / ecx, shr dl / dh
    if (h == 0) return;
    const int pitch = g_ui_fb.width, fbw = g_ui_fb.width, fbh = g_ui_fb.height;
    const uint8_t *p = s->data, *end = s->data + s->size;
    const uint8_t *blend = g_blend_table(), *shade = g_shade_table();
    const uint8_t colour = (uint8_t)arg;
    for (int row = 0; row < h; row++) {
        const int py = y + row;
        int px = x;
        for (;;) {
            if (p >= end) return;
            const int8_t n = (int8_t)*p++;
            if (n == 0) break;
            if (n < 0) { px += -n; continue; }
            if (p + n > end) return;
            if (row >= first_row && py >= 0 && py < fbh) {
                uint8_t *dst = g_ui_fb.pixels + py * pitch;
                for (int i = 0; i < n; i++) {
                    const int dx = px + i;
                    if (dx < 0 || dx >= fbw) continue;
                    const uint8_t src = p[i];
                    switch (mode) {
                    case BLIT_COPY:  dst[dx] = src; break;
                    case BLIT_SOLID: dst[dx] = colour; break;
                    case BLIT_BLEND: dst[dx] = blend[(dst[dx] << 8) | src]; break;
                    case BLIT_TINT:  dst[dx] = blend[(dst[dx] << 8) | colour]; break;
                    case BLIT_SHADE: dst[dx] = shade[(colour << 8) | src]; break;
                    }
                }
            }
            p += n; px += n;
        }
    }
    (void)w;
}

// ui_draw_sprite_60688 (identical copy vga_fill_rect_606c0; ui_draw_icon_4d0dd): registers
// ebx = x, ecx = y, dl / dh = size, esi = data, two zero stack args -> vga_draw_sprite_spans_6070d.
void ui_draw_sprite(int x, int y, const UiSprite *s) {
    blit_spans(x, y, s, (g_ui_blit_flags & 0x40) ? BLIT_SOLID : BLIT_COPY, 0);
}
// ui_draw_glyph_4d240(x, y, entry, colour): the blitter with DAT_0009e5c4 = 0x40 set by ui_draw_text.
void ui_draw_glyph(int x, int y, const UiSprite *s, int colour) { blit_spans(x, y, s, BLIT_SOLID, colour); }
// ui_fill_rect_blend_224e0(x, y, entry)
void ui_draw_sprite_blend(int x, int y, const UiSprite *s) { blit_spans(x, y, s, BLIT_BLEND, 0); }
// ui_fill_rect_blend2_22610(x, y, entry, colour)
void ui_draw_sprite_tint(int x, int y, const UiSprite *s, int colour) { blit_spans(x, y, s, BLIT_TINT, colour); }
// ui_draw_sprite_shaded_22760(x, y, entry, level): colour operand is `level << 8` in the original
void ui_draw_sprite_shaded(int x, int y, const UiSprite *s, int level) { blit_spans(x, y, s, BLIT_SHADE, level); }
// ui_draw_icon_rows_4d1c8(x, y, entry, skip): y += skip, height -= skip (nothing when that is 0)
void ui_draw_sprite_rows(int x, int y, const UiSprite *s, int skip_rows) {
    if (!s || (uint8_t)(s->h - skip_rows) == 0) return;
    blit_spans(x, y, s, BLIT_COPY, 0, skip_rows);
}

// ---------------------------------------------------------------------------------------------
// rectangles
// ---------------------------------------------------------------------------------------------

// gfx_fill_rect_320_60590 / gfx_fill_rect_640_60610 (x, y, w, h, colour): 16-bit arguments, the 320
// variant shifts x / y / w / h right by one. No clipping in the original (a port-only guard here);
// the DAT_0009e5c4 & 4 blend path reads through the null table pointer DAT_0009e858 and is dead.
void gfx_fill_rect(int x, int y, int w, int h, int colour) {
    if (!g_ui_fb.pixels) return;
    x = (uint16_t)x; y = (uint16_t)y; w = (uint16_t)w; h = (uint16_t)h;
    if (ui_lo_res()) { x >>= 1; y >>= 1; w >>= 1; h >>= 1; }
    if (w <= 0 || h <= 0) return;      // the original would run 65536 rows for h == 0
    const int x0 = x < 0 ? 0 : x, x1 = x + w > g_ui_fb.width ? g_ui_fb.width : x + w;
    const int y0 = y < 0 ? 0 : y, y1 = y + h > g_ui_fb.height ? g_ui_fb.height : y + h;
    for (int yy = y0; yy < y1; yy++)
        if (x1 > x0) std::memset(g_ui_fb.pixels + yy * g_ui_fb.width + x0, colour, (size_t)(x1 - x0));
}

// gfx_fill_rows_320_4ce83 / gfx_fill_rows_640_4cea9 (buffer, rows, colour): rows * 320 (640) bytes.
void gfx_fill_rows(int rows, int colour) {
    if (!g_ui_fb.pixels) return;
    size_t n = (size_t)(uint16_t)rows * (size_t)g_ui_fb.width;
    const size_t cap = (size_t)g_ui_fb.width * (size_t)g_ui_fb.height;
    if (n > cap) n = cap;
    std::memset(g_ui_fb.pixels, colour, n);
}

// gfx_put_pixel_320_60f3c / gfx_put_pixel_640_60f7c (x, y, colour): unsigned 16-bit coordinates,
// rejected at >= 640 / >= 400 (320 mode: halved afterwards) or >= 480.
void gfx_put_pixel(int x, int y, int colour) {
    if (!g_ui_fb.pixels) return;
    const unsigned ux = (uint16_t)x, uy = (uint16_t)y;
    if (ux >= 0x280) return;
    if (ui_lo_res()) {
        if (uy >= 400) return;
        const int px = (int)ux >> 1, py = (int)uy >> 1;
        if (px < g_ui_fb.width && py < g_ui_fb.height) g_ui_fb.pixels[py * g_ui_fb.width + px] = (uint8_t)colour;
    } else {
        if (uy >= 0x1e0) return;
        if ((int)ux < g_ui_fb.width && (int)uy < g_ui_fb.height) g_ui_fb.pixels[uy * g_ui_fb.width + ux] = (uint8_t)colour;
    }
}

// ui_shade_rect_23310(x, y, w, h, level): dest = SHADE[level << 8 | dest]; 16-bit arguments halved
// (toward zero) in the 320 mode.
void ui_shade_rect(int x, int y, int w, int h, int level) {
    if (!g_ui_fb.pixels) return;
    int16_t sx = (int16_t)x, sy = (int16_t)y, sw = (int16_t)w, sh = (int16_t)h;
    if (ui_lo_res()) { sx /= 2; sy /= 2; sw /= 2; sh /= 2; }
    const uint8_t *shade = g_shade_table() + ((level & 0xff) << 8);
    for (int yy = sy; yy < sy + sh; yy++) {
        if (yy < 0 || yy >= g_ui_fb.height) continue;
        uint8_t *row = g_ui_fb.pixels + yy * g_ui_fb.width;
        for (int xx = sx; xx < sx + sw; xx++)
            if (xx >= 0 && xx < g_ui_fb.width) row[xx] = shade[row[xx]];
    }
}

// vga_fill_rect_clipped_6abbc (320: coordinates and the clip rectangle doubled, then >> 1) /
// gfx_fill_rect_clipped_6acd4 (640), mode 0: a vertical run when x1 == x0 ((y1 - y0) pixels), else a
// horizontal run of (x1 - x0) pixels; both ends are clamped to the clip rectangle (the screen).
void vga_fill_line(int x0, int y0, int x1, int y1, int colour) {
    if (!g_ui_fb.pixels) return;
    const bool lo = ui_lo_res();
    const int cx0 = 0, cy0 = 0;                         // DAT_0012ed88 / ed98 (* 2 in the 320 variant)
    const int cx1 = lo ? g_ui_fb.width * 2 : g_ui_fb.width;    // DAT_0012eda4
    const int cy1 = lo ? g_ui_fb.height * 2 : g_ui_fb.height;  // DAT_0012ed90
    int16_t ax0 = (int16_t)x0, ay0 = (int16_t)y0, ax1 = (int16_t)x1, ay1 = (int16_t)y1;
    if (ax0 < cx0) ax0 = (int16_t)cx0;
    if (ax1 < cx0) return;
    if (ax0 >= cx1) return;
    if (ax1 >= cx1) ax1 = (int16_t)cx1;
    if (ay0 < cy0) ay0 = (int16_t)cy0;
    if (ay1 < cy0) return;
    if (ay0 >= cy1) return;
    if (ay1 >= cy1) ay1 = (int16_t)cy1;
    const int px = lo ? ((uint16_t)ax0 >> 1) : ax0, py = lo ? ((uint16_t)ay0 >> 1) : ay0;
    if ((uint16_t)(ax1 - ax0) == 0) {
        int n = lo ? ((uint16_t)(ay1 - ay0) >> 1) : (uint16_t)(ay1 - ay0);
        for (int i = 0; i < n; i++)
            if (py + i < g_ui_fb.height && px < g_ui_fb.width) g_ui_fb.pixels[(py + i) * g_ui_fb.width + px] = (uint8_t)colour;
    } else {
        int n = lo ? ((uint16_t)(ax1 - ax0) >> 1) : (uint16_t)(ax1 - ax0);
        for (int i = 0; i < n; i++)
            if (px + i < g_ui_fb.width && py < g_ui_fb.height) g_ui_fb.pixels[py * g_ui_fb.width + px + i] = (uint8_t)colour;
    }
}

// vga_draw_box_603f0 (320) / vga_draw_rect_outline_640_604c0 (640) (x, y, w, h, colour): four runs.
void vga_draw_box(int x, int y, int w, int h, int colour) {
    const int w1 = w - 1, h1 = h - 1;
    vga_fill_line(x, y, x + w1, y, colour);                 // top
    vga_fill_line(x + w1, y, x + w1, y + h1 + 1, colour);   // right
    vga_fill_line(x, y + h1, x + w1 + 1, y + h1, colour);   // bottom
    vga_fill_line(x, y, x, y + h1, colour);                 // left
}

// ui_copy_block_23140(x0, y0, x1, y1): 320 mode 0x14 dwords x 0x32 rows, 640 mode 0x28 dwords x 100.
void ui_copy_block(int x0, int y0, int x1, int y1) {
    if (!g_ui_fb.pixels) return;
    const bool lo = ui_lo_res();
    const int bw = lo ? 0x50 : 0xa0, bh = lo ? 0x32 : 100;
    int sx = (int16_t)x0, sy = (int16_t)y0, dx = (int16_t)x1, dy = (int16_t)y1;
    if (lo) { sx /= 2; sy /= 2; dx /= 2; dy /= 2; }
    for (int r = 0; r < bh; r++) {
        const int ys = sy + r, yd = dy + r;
        if (ys < 0 || ys >= g_ui_fb.height || yd < 0 || yd >= g_ui_fb.height) continue;
        for (int c = 0; c < bw; c++) {
            const int xs = sx + c, xd = dx + c;
            if (xs < 0 || xs >= g_ui_fb.width || xd < 0 || xd >= g_ui_fb.width) continue;
            g_ui_fb.pixels[yd * g_ui_fb.width + xd] = g_ui_fb.pixels[ys * g_ui_fb.width + xs];
        }
    }
}

// ---------------------------------------------------------------------------------------------
// HUD text
// ---------------------------------------------------------------------------------------------

// ui_set_font_4abe0(n): DAT_000adfbc = DAT_000adf28[n], or DAT_000adf28[0] when that entry is null.
void ui_set_font(int n) {
    const UiSpriteTable *t = (n >= 0 && n < 2 && g_ui_font_tables[n].loaded) ? &g_ui_font_tables[n] : &g_ui_font_tables[0];
    g_ui_font = t;
}
static const UiSprite *font_glyph(unsigned c) { return ui_sprite(*g_ui_font, c + 1); }   // font + (c + 1) * 6
int ui_font_space_width() { return font_glyph(0x20)->w; }    // font + 0xca
int ui_font_line_height() { return font_glyph(0x20)->h; }    // font + 0xcb

// ui_draw_text_4a9a0(str, x, y, colour)
int ui_draw_text(const char *s, int x, int y, int colour) {
    g_ui_blit_flags = 0x40;
    const int x_start = x;
    if (!s || !*s) { g_ui_blit_flags = 0; return x; }
    for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
        if ((int16_t)x >= 0x280) break;
        const unsigned c = *p;
        if (c == 9 || c == 0x20) { x += ui_font_space_width(); continue; }
        if (c == 10) { y += ui_font_line_height(); x = x_start; continue; }
        if (c == 13) continue;
        const UiSprite *g = font_glyph(c);
        ui_draw_glyph((int16_t)x, (int16_t)y, g, colour);
        x += g->w;
    }
    g_ui_blit_flags = 0;
    return x;
}

// ui_draw_text_background_4aa90(str, x, y, colour): the same walk with ui_fill_rect_blend2_22610
// per glyph (the colour argument is what the original passes on to 22610).
int ui_draw_text_background(const char *s, int x, int y, int colour) {
    const int x_start = x;
    if (!s || !*s) return x;
    for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
        if ((int16_t)x >= 0x280) break;
        const unsigned c = *p;
        if (c == 9 || c == 0x20) { x += ui_font_space_width(); continue; }
        if (c == 10) { ui_font_line_height(); x = x_start; continue; }   // the original advances nothing but x here
        if (c == 11 || c == 12 || c == 13) continue;
        const UiSprite *g = font_glyph(c);
        ui_draw_sprite_tint((int16_t)x, (int16_t)y, g, colour);
        x += g->w;
    }
    return x;
}

// ui_text_width_4ab60(str): control bytes 1..8 are ignored, 9 / 0x20 count as a space.
int ui_text_width(const char *s) {
    int w = 0;
    if (!s) return 0;
    for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
        const unsigned c = *p;
        if (c < 9) continue;
        if (c == 9 || c == 0x20) w += ui_font_space_width();
        else w += font_glyph(c)->w;
    }
    return w;
}

// ---------------------------------------------------------------------------------------------
// front-end font engine
// ---------------------------------------------------------------------------------------------

void ui_set_clip_rect(int x, int y, int w, int h) {          // ui_set_clip_rect_588b0
    g_ui_clip.x0 = x; g_ui_clip.x1 = x + w; g_ui_clip.y0 = y; g_ui_clip.y1 = y + h; g_ui_clip.w = w; g_ui_clip.h = h;
}
void ui_get_clip_rect(UiClipRect *out) { *out = g_ui_clip; }   // ui_get_clip_rect_588f0
void ui_push_clip_rect() { s_clip_saved = g_ui_clip; }         // ui_push_clip_rect_58930 (also zeroes DAT_0012ebd8)
void ui_pop_clip_rect() { g_ui_clip = s_clip_saved; }          // ui_pop_clip_rect_58950

// ui_font_init_589d0(font, table): ui_font_relocate_glyphs_58f30, glyphs = table + 6 (entry 1 =
// space), colour1 = nearest(255, 255, 255), colour2 = nearest(0, 0, 0), flags = 3.
void ui_font_init(FontDesc *font, const UiSpriteTable *table) {
    if (table && table->loaded && table->entries.size() > 1) {
        font->glyphs = &table->entries[1];
        font->count = (unsigned)table->entries.size() - 1;
    } else {
        font->glyphs = nullptr; font->count = 0;
    }
    font->colour1 = (uint8_t)palette_find_nearest(g_palette6, 0xff, 0xff, 0xff);
    font->colour2 = (uint8_t)palette_find_nearest(g_palette6, 0, 0, 0);
    font->flags = 3;
}
static const UiSprite *fe_glyph(const FontDesc *font, unsigned c) {
    const unsigned i = (uint8_t)(c - 0x20);
    return (font->glyphs && i < font->count) ? &font->glyphs[i] : &s_null_sprite;
}

// ui_text_width_58970(font, str): widest line; a glyph advances by width - 1.
int ui_fe_text_width(const FontDesc *font, const char *s) {
    int16_t best = 0, cur = 0;
    for (const char *p = s; p && *p; p++) {
        const char c = *p;
        if (c < 0x20) {
            if (c == '\n') { if (best < cur) best = cur; cur = 0; }
        } else {
            cur = (int16_t)(cur + (fe_glyph(font, (uint8_t)c)->w - 1));
        }
    }
    if (best < cur) best = cur;
    return best;
}

// ui_draw_glyph_58ba0(font, x, y, glyph): clipped to g_ui_clip (x / y are relative to its origin),
// draw mode 0: pixel value 1 -> colour1 (flags & 1), 2 -> colour2 (flags & 2), nothing else is written.
void ui_fe_draw_glyph(int x, int y, const FontDesc *font, const UiSprite *g) {
    if (!g || !g->data || !g_ui_fb.pixels) return;
    const int16_t sx = (int16_t)(x + g_ui_clip.x0), sy = (int16_t)(y + g_ui_clip.y0);
    int w = g->w, h = g->h;
    // the original rejects glyphs entirely outside the clip rect and trims the rest (16-bit math)
    if (sx + w <= g_ui_clip.x0 || sx >= g_ui_clip.x1 || sy + h <= g_ui_clip.y0 || sy >= g_ui_clip.y1) return;
    const unsigned mode = font->flags & 0xc000;
    const uint8_t *p = g->data, *end = g->data + g->size;
    for (int row = 0; row < h; row++) {
        const int py = sy + row;
        int px = sx;
        for (;;) {
            if (p >= end) return;
            const int8_t n = (int8_t)*p++;
            if (n == 0) break;
            if (n < 0) { px += -n; continue; }
            if (p + n > end) return;
            if (mode == 0 && py >= g_ui_clip.y0 && py < g_ui_clip.y1 && py >= 0 && py < g_ui_fb.height) {
                uint8_t *dst = g_ui_fb.pixels + py * g_ui_fb.width;
                for (int i = 0; i < n; i++) {
                    const int dx = px + i;
                    if (dx < g_ui_clip.x0 || dx >= g_ui_clip.x1 || dx < 0 || dx >= g_ui_fb.width) continue;
                    const uint8_t v = p[i];
                    if (v == 1 && (font->flags & 1)) dst[dx] = font->colour1;
                    else if (v == 2 && (font->flags & 2)) dst[dx] = font->colour2;
                }
            }
            p += n; px += n;
        }
    }
}

// ui_draw_text_58ab0(x, y, font, str)
void ui_fe_draw_text(int x, int y, FontDesc *font, const char *s) {
    if (!s) return;
    const int x_start = x;
    int16_t cx = (int16_t)x;
    for (const uint8_t *p = (const uint8_t *)s; *p;) {
        const uint8_t c = *p;
        if (c < 0x20) {
            switch (c) {
            case 1: if (p[1]) { p++; font->colour1 = *p; } break;
            case 2: if (p[1]) { p++; font->colour2 = *p; } break;
            case 3: font->flags |= 3; break;
            case 4: font->flags &= 0xfffc; break;
            case 5: font->flags |= 5; break;
            case 6: font->flags &= 0xfffa; break;
            case 10: y += fe_glyph(font, 0x20)->h; cx = (int16_t)x_start; break;
            default: break;
            }
            p++;
            continue;
        }
        const UiSprite *g = fe_glyph(font, c);
        p++;
        if (!(font->flags & 0x10)) {
            ui_fe_draw_glyph(cx, y, font, g);
            cx = (int16_t)(cx + (g->w - 1));
        } else {
            if (c != 0x20) ui_draw_sprite(cx, y, g);         // vga_fill_rect_606c0 = the span blitter
            cx = (int16_t)(cx + (g->w - 2));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// debug_screenshot_3ca00: PPM instead of the "mhwanh" raw dump.
// ---------------------------------------------------------------------------------------------

int debug_screenshot(const char *dir) {
    if (!g_ui_fb.pixels) return -1;
    char path[1024];
    int n = 0;
    for (; n < 10000; n++) {
        std::snprintf(path, sizeof path, "%s%sscr%05d.ppm", dir ? dir : "", (dir && *dir) ? "/" : "", n);
        FILE *f = std::fopen(path, "rb");
        if (!f) break;
        std::fclose(f);
    }
    if (n == 10000) return -1;
    FILE *f = std::fopen(path, "wb");
    if (!f) return -1;
    std::fprintf(f, "P6\n%d %d\n255\n", g_ui_fb.width, g_ui_fb.height);
    uint8_t rgb[768];
    mc_palette_to_rgb(g_palette6, rgb);
    for (int i = 0; i < g_ui_fb.width * g_ui_fb.height; i++) std::fwrite(rgb + g_ui_fb.pixels[i] * 3, 1, 3, f);
    std::fclose(f);
    return n;
}

// ---------------------------------------------------------------------------------------------
// mouse pointer into the back buffer (round 6, render reference)
// ---------------------------------------------------------------------------------------------

// mouse_cursor_set_sprite_5ba5c + mouse_cursor_draw_5b35c. The original draws its software pointer
// INTO the back buffer when it blits a frame (vga_copy_320x200_610f0 / the VESA copies call
// mouse_cursor_hide_for_blit_5b7f8 while the mouse is present, DAT_0009e5e4): set_sprite renders the
// sprite with ui_draw_sprite_60688 at (0, 0) into a 64x64 buffer pre-filled with 0xfe (size = the
// tab entry's w / h bytes, halved in 320x200), draw copies every byte != 0xfe to the mouse position
// (DAT_0009e5dc / de, halved with `sar` in 320x200; hot spot top-left), clipped to the screen's right
// and bottom edges (the saved background is not put back before the next frame is drawn). Only a
// frame that does not overwrite the whole back buffer shows it afterwards: the motion-blur blend of
// the next frame (render_reference2_test: closing the book with blur on). In game the pointer is
// pointers entry 1 while the local player's input mode is 2 (the book) and entry 0 (empty) otherwise
// (player_set_input_mode_3bb50, video_input_init_3ed60).
void ui_draw_mouse_pointer(const FrameBuffer &fb, const UiSprite *s, int mouse_x, int mouse_y) {
    if (!fb.pixels || !s || !s->data) return;
    static uint8_t cursor[64 * 64];                     // DAT_0012edf8
    std::memset(cursor, 0xfe, sizeof cursor);
    const FrameBuffer saved = g_ui_fb;
    ui_set_target(FrameBuffer{cursor, 64, 64});
    ui_draw_sprite(0, 0, s);
    ui_set_target(saved);
    int16_t cw = s->w, ch = s->h;                       // DAT_0012edf4 / f6
    int16_t x = (int16_t)mouse_x, y = (int16_t)mouse_y;
    if (ui_lo_res()) { cw = (int16_t)(cw >> 1); ch = (int16_t)(ch >> 1); x = (int16_t)(x >> 1); y = (int16_t)(y >> 1); }
    int16_t w = (int16_t)(fb.width - x), h = (int16_t)(fb.height - y);   // 0x5b3da..0x5b448
    if (w > cw) w = cw;
    if (h > ch) h = ch;
    if (x < 0 || y < 0) return;                         // (the mouse range keeps the pointer on screen)
    for (int r = 0; r < h; r++) {
        uint8_t *d = fb.pixels + (size_t)(y + r) * (size_t)fb.width + x;
        const uint8_t *c = cursor + r * 64;
        for (int k = 0; k < w; k++)
            if (c[k] != 0xfe) d[k] = c[k];
    }
}
