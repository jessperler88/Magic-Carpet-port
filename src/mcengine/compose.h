// Port-only (Phase 3, round 7 task B): native-resolution frame composition. Nothing here exists in
// carpet.exe. Report: docs/analysis/port_compose.md.
//
// The game draws everything (3D view, HUD, spell book, map, messages, pointer) into one 8-bit game frame
// of 320x200 / 640x480 (render_frame_draw, hud.cpp). In the composed ("native") mode:
//   - the view override (render.h g_render_view_override, installed by compose_install) renders the 3D
//     view with render_view_ext into a high-resolution 8-bit VIEW buffer - the whole display when the game's
//     view window is the whole frame (flight), otherwise the display rectangle of the window (reduced view
//     sizes, the map screen's small view) - and point-samples it down into the game frame's view window,
//     so the 2D pass that follows (translucent HUD panels included) draws over a matching picture;
//   - after the 2D pass the frame is compared with the copy taken right after the view was placed: pixels
//     the 2D pass changed form the HUD LAYER (mask); every pixel outside the view window is HUD too. A
//     second pass over an altered view (compose_draw_frame) catches HUD pixels that happen to have the
//     colour of the view under them;
//   - presentation = the hi-res view, then the HUD layer scaled on top through a list of blits (the 4:3
//     game frame centred, its top corner blocks optionally moved to the display's corners), palette
//     applied at the very end. Frames without a view (front end, full-screen 2D screens) are the plain
//     game frame, 4:3, centred with black bars.
// With the override not installed nothing here runs and the game frame is exactly today's.
//
// No SDL here: the platform (src/mcport/platform_sdl.cpp) presents a ComposeOutput; compose_to_rgb is
// the reference CPU compositor (tests, screenshots).
#pragma once
#include <cstdint>
#include "render.h"

// One rectangle of the game frame (sx, sy, sw, sh) drawn scaled into the display rectangle (dx, dy, dw, dh).
struct ComposeBlit { int sx, sy, sw, sh, dx, dy, dw, dh; };

struct ComposeOutput {
    int display_w = 0, display_h = 0;
    // The game frame's mapping: the whole frame (game_w x game_h) <-> display rectangle hud_*.
    int game_w = 0, game_h = 0;
    int hud_x = 0, hud_y = 0, hud_w = 0, hud_h = 0;
    // The hi-res 3D view (only when has_view): view_buf_w x view_buf_h palette indices, shown scaled
    // into the display rectangle view_x, view_y, view_w, view_h (buffer size == rect size unless
    // g_settings.view_width / view_height ask for a lower render resolution).
    bool has_view = false;
    const uint8_t *view = nullptr;
    int view_buf_w = 0, view_buf_h = 0;
    int view_x = 0, view_y = 0, view_w = 0, view_h = 0;
    // The game frame after the 2D pass (game_w x game_h) and the HUD mask (1 = HUD pixel, drawn opaque;
    // 0 = the view shows through). mask == nullptr: the whole frame is opaque (no view this frame).
    const uint8_t *hud = nullptr;
    const uint8_t *mask = nullptr;
    // How the HUD layer is drawn: blits[0..blit_count) cover the game frame exactly once.
    ComposeBlit blits[8];
    int blit_count = 0;
    bool hud_filtered = false;      // g_settings.hud_scale_mode == 2: linear filtering for the HUD layer
    bool corners_anchored = false;  // the top corner blocks were moved to the display corners
};

// ---- installation (the override) ----
void compose_install();             // g_render_view_override = the compositor's view renderer
void compose_remove();              // back to render_view (the original); compose_output() reports no view
bool compose_installed();

// ---- per frame ----
// Start a frame for a display (window drawable) of display_w x display_h pixels and the game frame `fb`.
void compose_begin_frame(int display_w, int display_h, const FrameBuffer &fb);
// After the first pass: true when the pass placed a view (a second pass refines the HUD mask).
bool compose_need_second_pass();
// Saves the first pass's frame and mask, puts the altered view into the window for the second pass.
void compose_begin_second_pass(const FrameBuffer &fb);
// HUD diff; restores the first pass's frame into `fb` when a second pass ran; fills compose_output().
void compose_end_frame(const FrameBuffer &fb);
// The whole frame in one call: draw() is the 2D pass (render_frame_draw + overlay + pointer); it runs
// twice when a view was placed (the second run's pixels are thrown away, only its diff is used).
template <class F> void compose_draw_frame(int display_w, int display_h, const FrameBuffer &fb, F &&draw) {
    compose_begin_frame(display_w, display_h, fb);
    draw();
    if (compose_need_second_pass()) { compose_begin_second_pass(fb); draw(); }
    compose_end_frame(fb);
}
// The last finished frame (valid until the next compose_end_frame / compose_remove).
const ComposeOutput &compose_output();
// What to present now: the frame composed since the last call if there is one; otherwise, when `fb`
// still holds that frame's pixels (a palette fade re-presents it), the same output again; otherwise
// `fb` as a plain frame without a view (front end, title / result screens, anything drawn outside
// compose_draw_frame): 4:3, centred, black bars.
const ComposeOutput &compose_frame_for_present(int display_w, int display_h, const FrameBuffer &fb);

// ---- layout helpers (pure; exposed for the platform and the tests) ----
// The display rectangle of a game frame of game_w x game_h (shown at 4:3) for hud_scale_mode
// (0 = integer scale where it fits, 1 = fit nearest, 2 = fit filtered), centred.
void compose_hud_rect(int display_w, int display_h, int game_w, int game_h, int scale_mode,
                      int *x, int *y, int *w, int *h);
// Mouse: a display (window drawable) pixel -> game-frame pixel through the HUD rectangle (the central
// 4:3 area; clamped to the frame), and back (the frame pixel's centre). These use compose_output()'s
// mapping; with no frame composed yet they use a centred fit for the given sizes.
void compose_display_to_frame(const ComposeOutput &o, int dx, int dy, int *fx, int *fy);
void compose_frame_to_display(const ComposeOutput &o, int fx, int fy, int *dx, int *dy);
// Round 10 (task C): a display pixel -> the hi-res view buffer's pixel (render.h "view space" of a composed
// frame: picks at display precision); false when the frame has no view or the pixel lies outside it. And
// back (the view pixel's centre, unclamped).
bool compose_display_to_view(const ComposeOutput &o, int dx, int dy, int *vx, int *vy);
void compose_view_to_display(const ComposeOutput &o, double vx, double vy, double *dx, double *dy);

// ---- reference CPU compositor ----
// out = display_w * display_h * 3 bytes RGB; palette6 = 256 * 3 VGA 6-bit values (g_display_palette6).
// Nearest-pixel sampling for view and HUD (the platform's GPU path may filter in hud_scale_mode 2).
void compose_to_rgb(const ComposeOutput &o, const uint8_t *palette6, uint8_t *out_rgb);
