// 2D drawing layer of the HUD and the front end (carpet.exe 0x4a9a0..0x4ac00, 0x4d096..0x4d2b0,
// 0x224e0..0x233c0, 0x603f0..0x60f7c (vga_draw_sprite_spans_6070d), 0x4ce83 / 0x4cea9, 0x588b0..
// 0x58f60, 0x49de0, 0x60fc0 and the colour cube of data_load_all_334c0). Owner: ui_draw.cpp (same
// agent as hud.cpp). Report: docs/analysis/port_hud.md.
//
// Coordinates: every UI coordinate and size is in the 640-wide virtual screen (640x480, or 640x400
// when g_video_mode_flags & 1). The blitters halve position and size when g_video_mode_flags & 1
// (vga_draw_sprite_spans_6070d, gfx_fill_rect_320_60590, ...), exactly as the original does; the
// sprite tables loaded in that mode carry doubled width / height bytes (sprite_table_relocate_x2_62a80)
// so that the halving yields the real pixel size. All 2D output goes into the frame buffer given to
// ui_set_target() (DAT_0012ed74 / ed70 / ed78) and is clipped to it (the original clips sprites to
// the 2D clip rectangle = the whole screen and does not clip rectangle fills at all).
#pragma once
#include <cstdint>
#include <vector>
#include "render.h"
#include "sprite.h"

// ---- sprite tables (.dat + .tab, span encoded) ---------------------------------------------------
// One relocated 6-byte .tab entry: {u32 pointer to the span data, u8 width, u8 height}. In the
// 320x200 mode width / height hold twice the pixel size (see above).
struct UiSprite {
    const uint8_t *data;    // span rows: n>0 copy n bytes, n<0 skip -n pixels, 0 end of row
    uint32_t       size;    // port only: bytes readable from `data` (reads past it count as end of sprite)
    uint8_t        w, h;    // in 640-space (doubled in the 320x200 mode)
};
struct UiSpriteTable {
    mc_sprite_set         set;      // the loaded files (mcdata/sprite.h)
    std::vector<UiSprite> entries;  // relocated entries (ui_sprite_lists_relocate_49de0)
    bool                  loaded = false;
};
// Entry `index` of a table, or a null sprite {nullptr, 0, 0, 0} when out of range / not loaded.
const UiSprite *ui_sprite(const UiSpriteTable &t, unsigned index);

extern UiSpriteTable g_hud_sprites;        // DAT_000adf94: data/hspr0-0 (640x480) or data/mspr0-0 (320x200)
extern UiSpriteTable g_ui_font_tables[2];  // DAT_000adf28 / DAT_000adf2c: data/font0, data/font1 (glyph c = entry c + 1)
extern UiSpriteTable g_ui_pointers;        // DAT_000adfc0 / adfb8: data/pointers (mouse pointer sprites)
extern UiSpriteTable g_ui_building;        // DAT_000adfb0 / adf98: data/building
inline const UiSprite *hud_sprite(unsigned index) { return ui_sprite(g_hud_sprites, index); }

// Loads data/font0, font1, pointers, building and the HUD sprite set for the current
// g_video_mode_flags, builds the colour cube from g_palette6 (tables_load_palette first) and selects
// font 0 (end of data_load_all_334c0). False when a file is missing (fonts and HUD sprites are
// required, pointers / building optional).
bool ui_draw_init(const char *game_dir);
void ui_draw_shutdown();
// video_toggle_resolution_33600 half: reloads the HUD sprite set that belongs to the new
// g_video_mode_flags and re-relocates every table (ui_sprite_lists_relocate_49de0). Call after
// changing g_video_mode_flags.
bool ui_draw_set_video_mode(const char *game_dir);
// ui_sprite_lists_relocate_49de0: recompute the entry sizes of every loaded table for the current mode.
void ui_sprite_lists_relocate();

// ---- target --------------------------------------------------------------------------------------
// The whole-screen back buffer the 2D code draws into (DAT_0012ed74, pitch DAT_0012ed70, height
// DAT_0012ed78; 2D clip rectangle DAT_0012ed88/98/80/a8 = the whole buffer). render_frame sets it.
extern FrameBuffer g_ui_fb;
void ui_set_target(const FrameBuffer &fb);
inline bool ui_lo_res() { extern uint16_t g_video_mode_flags; return (g_video_mode_flags & 1) != 0; }

// ---- colour cube (DAT_000acc18, 16 x 16 x 16) ---------------------------------------------------
// palette_find_nearest_60fc0(pal6, r, g, b): index of the palette entry closest (squared distance)
// to the 6-bit colour; the first of equal candidates wins.
int  palette_find_nearest(const uint8_t *pal6, int r, int g, int b);
// data_load_all_334c0: cube[(r << 8) | (g << 4) | b] = palette_find_nearest(pal, r * 4, g * 4, b * 4).
void ui_colour_cube_build(const uint8_t *pal6);
extern uint8_t g_colour_cube[4096];
inline uint8_t ui_colour(int r, int g, int b) { return g_colour_cube[((r & 15) << 8) | ((g & 15) << 4) | (b & 15)]; }
// The cube entries the HUD code uses as literal addresses.
inline uint8_t ui_col_black() { return g_colour_cube[0x000]; }   // DAT_000acc18
inline uint8_t ui_col_white() { return g_colour_cube[0xfff]; }   // DAT_000adc17
inline uint8_t ui_col_red()   { return g_colour_cube[0xf00]; }   // DAT_000adb18
inline uint8_t ui_col_green() { return g_colour_cube[0x0f0]; }   // DAT_000acd08
inline uint8_t ui_col_blue()  { return g_colour_cube[0x00f]; }   // DAT_000acc27
inline uint8_t ui_col_magenta() { return g_colour_cube[0xf0f]; } // DAT_000adb27

// ---- span sprite blitter (vga_draw_sprite_spans_6070d and the C wrappers around it) ---------------
// Blit flag word DAT_0009e5c4 as the original keeps it: 1 mirror x, 2 flip y, 4 blend through the
// table pointer DAT_0009e858 (null in the exe: unusable), 8 downscale, 0x20 upscale, 0x40 solid
// colour (text). Only 0 and 0x40 are reachable from the game code; the port implements those.
extern uint16_t g_ui_blit_flags;
void ui_draw_sprite(int x, int y, const UiSprite *s);                      // ui_draw_sprite_60688 / vga_fill_rect_606c0 / ui_draw_icon_4d0dd: plain copy, 0 transparent
void ui_draw_glyph(int x, int y, const UiSprite *s, int colour);           // ui_draw_glyph_4d240: every span pixel = colour
void ui_draw_sprite_blend(int x, int y, const UiSprite *s);                // ui_fill_rect_blend_224e0: dest = BLEND[dest << 8 | src]
void ui_draw_sprite_tint(int x, int y, const UiSprite *s, int colour);     // ui_fill_rect_blend2_22610: dest = BLEND[dest << 8 | colour] under the sprite's pixels
void ui_draw_sprite_shaded(int x, int y, const UiSprite *s, int level);    // ui_draw_sprite_shaded_22760: dest = SHADE[level << 8 | src]
void ui_draw_sprite_rows(int x, int y, const UiSprite *s, int skip_rows);  // ui_draw_icon_rows_4d1c8: the sprite from row `skip_rows` on, at y + skip_rows

// ---- rectangles ----------------------------------------------------------------------------------
void gfx_fill_rect(int x, int y, int w, int h, int colour);     // gfx_fill_rect_320_60590 / gfx_fill_rect_640_60610 by mode (DAT_0009e5c4 & 4 blend path is dead)
void gfx_fill_rows(int rows, int colour);                       // gfx_fill_rows_320_4ce83 / gfx_fill_rows_640_4cea9: the first `rows` rows of the target (screen pixels)
void gfx_put_pixel(int x, int y, int colour);                   // gfx_put_pixel_320_60f3c / gfx_put_pixel_640_60f7c: bounds-checked against 640 x 400 / 480
void ui_shade_rect(int x, int y, int w, int h, int level);      // ui_shade_rect_23310: dest = SHADE[level << 8 | dest]
void vga_draw_box(int x, int y, int w, int h, int colour);      // vga_draw_box_603f0 / vga_draw_rect_outline_640_604c0: 1-pixel outline through vga_fill_rect_clipped_6abbc / 6acd4
void vga_fill_line(int x0, int y0, int x1, int y1, int colour); // vga_fill_rect_clipped_6abbc / gfx_fill_rect_clipped_6acd4 (mode 0): horizontal or vertical run, end exclusive
void ui_copy_block(int x0, int y0, int x1, int y1);             // ui_copy_block_23140: 160x50 (320) / 320x100 (640) pixel block copy inside the target

// ---- HUD text (ui_draw_text_4a9a0 family, font = the current sprite table DAT_000adfbc) ----------
extern const UiSpriteTable *g_ui_font;                          // DAT_000adfbc
void ui_set_font(int n);                                        // ui_set_font_4abe0: DAT_000adf28[n], table 0 when that is null
int  ui_font_space_width();                                     // ui_font_space_width_4abc0: width of glyph 0x20 (entry 33)
int  ui_font_line_height();                                     // ui_font_line_height_4abd0: height of glyph 0x20
// Draws `s` with the current font: 9 / 0x20 advance by the space width, 10 newline (x back to the
// start, y += line height), 13 nothing, every other byte c draws entry c + 1 and advances by its width;
// stops when x reaches 0x280. Returns the final x. DAT_0009e5c4 = 0x40 while drawing.
int  ui_draw_text(const char *s, int x, int y, int colour);
int  ui_draw_text_background(const char *s, int x, int y, int colour);   // ui_draw_text_background_4aa90: the same walk, every glyph as a translucent box (22610)
int  ui_text_width(const char *s);                                       // ui_text_width_4ab60: sum of the advances (no newline handling)

// ---- front-end font engine (0x588b0..0x58f60; FontDesc = DAT_0009e508 sfont0 ...) ---------------
// Not used by the HUD; ported for the front end. Coordinates are screen pixels (no halving).
struct FontDesc {
    const UiSprite *glyphs;     // +0: glyph table, glyph c = glyphs[c - 0x20] (the tab's entry 1 on)
    unsigned        count;      // port only: entries behind `glyphs`
    uint16_t        flags;      // +4: bit0 / bit1 remap pixel 1 / 2 to colour1 / colour2, 0x10 use the sprite blitter, 0xc000 draw mode
    uint8_t         colour1;    // +6
    uint8_t         colour2;    // +7
};
struct UiClipRect { int x0, x1, y0, y1, w, h; };                // DAT_0012ead0 / d4 / d8 / dc / e0 / e4
extern UiClipRect g_ui_clip;
void ui_set_clip_rect(int x, int y, int w, int h);              // ui_set_clip_rect_588b0
void ui_get_clip_rect(UiClipRect *out);                         // ui_get_clip_rect_588f0
void ui_push_clip_rect();                                       // ui_push_clip_rect_58930 (one level, DAT_0012eae8)
void ui_pop_clip_rect();                                        // ui_pop_clip_rect_58950
// ui_font_init_589d0(font, table): glyphs = table entries from 1, colours = nearest to white / black, flags = 3.
void ui_font_init(FontDesc *font, const UiSpriteTable *table);
int  ui_fe_text_width(const FontDesc *font, const char *s);     // ui_text_width_58970: widest line, glyph advance = width - 1
// ui_draw_text_58ab0(x, y, font, s): control bytes 1 / 2 (+ colour byte), 3..6 flag toggles, 10 newline.
void ui_fe_draw_text(int x, int y, FontDesc *font, const char *s);
void ui_fe_draw_glyph(int x, int y, const FontDesc *font, const UiSprite *g);   // ui_draw_glyph_58ba0 (draw mode 0, clipped to g_ui_clip)

// ---- debug_screenshot_3ca00 ------------------------------------------------------------------
// Writes the target as scrNNNNN.ppm (first free number below 10000) into `dir` with the palette
// g_palette6 (the original writes the Bullfrog "mhwanh" raw format). Returns the number or -1.
int debug_screenshot(const char *dir);
