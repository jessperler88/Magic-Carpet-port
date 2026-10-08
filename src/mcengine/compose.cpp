// Port-only (Phase 3, round 7 task B): native-resolution frame composition (compose.h).
// Report: docs/analysis/port_compose.md.
#include "compose.h"
#include "raster.h"
#include "settings.h"
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

bool s_installed = false;
bool s_fresh = false;               // s_out was composed since the last compose_frame_for_present
ComposeOutput s_out;                 // the last finished frame

// current frame
int s_disp_w = 0, s_disp_h = 0;
int s_game_w = 0, s_game_h = 0;
int s_hud_x = 0, s_hud_y = 0, s_hud_w = 0, s_hud_h = 0;
int s_pass = 0;                      // 0 = no frame open, 1 / 2 = pass of compose_draw_frame
bool s_view_placed = false;          // pass 1 placed a view
bool s_view_placed2 = false;         // pass 2 placed it again
int s_win_x = 0, s_win_y = 0, s_win_w = 0, s_win_h = 0;   // the game frame's view window
int s_vx = 0, s_vy = 0, s_vw = 0, s_vh = 0;               // its display rectangle
int s_vbw = 0, s_vbh = 0;                                 // hi-res view buffer size

std::vector<uint8_t> s_view;         // hi-res view (s_vbw x s_vbh)
std::vector<uint8_t> s_down;         // the downsampled view as placed into the window (s_win_w x s_win_h)
std::vector<uint8_t> s_final1;       // pass 1's frame (game_w x game_h)
std::vector<uint8_t> s_hud;          // finished frame copy for the output
std::vector<uint8_t> s_mask;         // HUD mask (game_w x game_h)

inline int64_t floordiv(int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }

// game frame column / row -> display (left / top edge of the pixel)
inline int map_x(int fx) { return s_hud_x + (int)floordiv((int64_t)fx * s_hud_w, s_game_w); }
inline int map_y(int fy) { return s_hud_y + (int)floordiv((int64_t)fy * s_hud_h, s_game_h); }

// The override: render_frame_1fab0's view call (render_view_frame) in composed mode.
void compose_view(const FrameBuffer &fb, const Camera &cam) {
    if (s_pass == 0 || fb.pixels == nullptr || fb.width != s_game_w || fb.height != s_game_h) {
        render_view(fb, cam);        // no frame open (or a foreign buffer): the original path
        return;
    }
    // The view window render_set_view_window / _top selected (pitch = frame width in both).
    const intptr_t off = g_rt_dest - fb.pixels;
    if (off < 0 || off >= (intptr_t)fb.width * fb.height) { render_view(fb, cam); return; }
    const int wx = (int)(off % fb.width), wy = (int)(off / fb.width);
    const int ww = g_rt_width, wh = g_rt_height;
    if (ww <= 0 || wh <= 0 || wx + ww > fb.width || wy + wh > fb.height) { render_view(fb, cam); return; }

    if (s_pass == 2 && s_view_placed) {
        // second pass: the altered view (every index changed) so a HUD pixel equal to the view under it
        // in pass 1 shows up as a change here
        if (ww != s_win_w || wh != s_win_h || wx != s_win_x || wy != s_win_y) return;   // not the same frame
        for (int y = 0; y < wh; ++y) {
            uint8_t *d = fb.pixels + (size_t)(wy + y) * fb.width + wx;
            const uint8_t *s = s_down.data() + (size_t)y * ww;
            for (int x = 0; x < ww; ++x) d[x] = (uint8_t)(s[x] ^ 0x80);
        }
        s_view_placed2 = true;
        return;
    }

    s_win_x = wx; s_win_y = wy; s_win_w = ww; s_win_h = wh;
    if (wx == 0 && wy == 0 && ww == fb.width && wh == fb.height) {
        s_vx = 0; s_vy = 0; s_vw = s_disp_w; s_vh = s_disp_h;          // flight: the whole display
    } else {
        s_vx = map_x(wx); s_vy = map_y(wy);
        s_vw = map_x(wx + ww) - s_vx; s_vh = map_y(wy + wh) - s_vy;
    }
    if (s_vw < 1) s_vw = 1;
    if (s_vh < 1) s_vh = 1;
    s_vbw = s_vw; s_vbh = s_vh;
    if (g_settings.view_width > 0 && g_settings.view_height > 0 && s_disp_w > 0 && s_disp_h > 0) {
        s_vbw = (int)((int64_t)s_vw * g_settings.view_width / s_disp_w);
        s_vbh = (int)((int64_t)s_vh * g_settings.view_height / s_disp_h);
        if (s_vbw < 1) s_vbw = 1;
        if (s_vbh < 1) s_vbh = 1;
    }
    s_view.resize((size_t)s_vbw * s_vbh);

    // render_view_ext keeps the raster target; saved here as well (the HUD code after us uses its own).
    uint8_t *rt_dest = g_rt_dest; uint8_t *rt_prev = g_rt_prev_row;
    const int rt_pitch = g_rt_pitch, rt_w = g_rt_width, rt_h = g_rt_height;
    render_view_ext(FrameBuffer{s_view.data(), s_vbw, s_vbh}, cam);
    g_rt_dest = rt_dest; g_rt_prev_row = rt_prev; g_rt_pitch = rt_pitch; g_rt_width = rt_w; g_rt_height = rt_h;
    // Round 10 (task C, render.h projection export): view-buffer pixel -> display -> game-frame pixel.
    if (s_hud_w > 0 && s_hud_h > 0 && s_vbw > 0 && s_vbh > 0) {
        const double kx = (double)s_game_w / s_hud_w, ky = (double)s_game_h / s_hud_h;
        render_view_info_set_frame_mapping((s_vx - s_hud_x) * kx, (s_vy - s_hud_y) * ky, (double)s_vw / s_vbw * kx,
                                           (double)s_vh / s_vbh * ky, s_game_w, s_game_h, true);
    }

    // Point-sample the view into the window: game pixel centre -> display -> view buffer.
    static std::vector<int> cols, rows;
    cols.resize((size_t)ww); rows.resize((size_t)wh);
    for (int x = 0; x < ww; ++x) {
        const int64_t d2 = 2 * (int64_t)s_hud_x * s_game_w + (int64_t)(2 * (wx + x) + 1) * s_hud_w;   // 2*display*game_w
        int64_t b = floordiv((floordiv(d2, 2 * (int64_t)s_game_w) - s_vx) * s_vbw, s_vw);
        cols[(size_t)x] = (int)(b < 0 ? 0 : b >= s_vbw ? s_vbw - 1 : b);
    }
    for (int y = 0; y < wh; ++y) {
        const int64_t d2 = 2 * (int64_t)s_hud_y * s_game_h + (int64_t)(2 * (wy + y) + 1) * s_hud_h;
        int64_t b = floordiv((floordiv(d2, 2 * (int64_t)s_game_h) - s_vy) * s_vbh, s_vh);
        rows[(size_t)y] = (int)(b < 0 ? 0 : b >= s_vbh ? s_vbh - 1 : b);
    }
    s_down.resize((size_t)ww * wh);
    for (int y = 0; y < wh; ++y) {
        const uint8_t *src = s_view.data() + (size_t)rows[(size_t)y] * s_vbw;
        uint8_t *dn = s_down.data() + (size_t)y * ww;
        for (int x = 0; x < ww; ++x) dn[x] = src[cols[(size_t)x]];
        std::memcpy(fb.pixels + (size_t)(wy + y) * fb.width + wx, dn, (size_t)ww);
    }
    s_view_placed = true;
}

// pixels of the window that differ from what was placed (pass 1: s_down, pass 2: s_down ^ 0x80)
void window_diff(const FrameBuffer &fb, uint8_t xor_v, bool or_into) {
    for (int y = 0; y < s_win_h; ++y) {
        const uint8_t *f = fb.pixels + (size_t)(s_win_y + y) * fb.width + s_win_x;
        const uint8_t *s = s_down.data() + (size_t)y * s_win_w;
        uint8_t *m = s_mask.data() + (size_t)(s_win_y + y) * s_game_w + s_win_x;
        for (int x = 0; x < s_win_w; ++x) {
            const uint8_t c = (uint8_t)(f[x] != (uint8_t)(s[x] ^ xor_v));
            m[x] = or_into ? (uint8_t)(m[x] | c) : c;
        }
    }
}

// Does a HUD shape cross the boundary between row y-1 and y (cols x0..x1)?
bool crosses_row(int y, int x0, int x1) {
    if (y <= 0 || y >= s_game_h) return false;
    const uint8_t *a = s_mask.data() + (size_t)(y - 1) * s_game_w, *b = s_mask.data() + (size_t)y * s_game_w;
    for (int x = x0; x < x1; ++x) if (a[x] && b[x]) return true;
    return false;
}

void add_blit(ComposeOutput &o, int sx, int sy, int sw, int sh, int shift_x, int shift_y) {
    if (sw <= 0 || sh <= 0 || o.blit_count >= 8) return;
    ComposeBlit &b = o.blits[o.blit_count++];
    b.sx = sx; b.sy = sy; b.sw = sw; b.sh = sh;
    b.dx = map_x(sx) + shift_x; b.dy = map_y(sy) + shift_y;
    b.dw = map_x(sx + sw) - map_x(sx); b.dh = map_y(sy + sh) - map_y(sy);
}

// HUD blocks in the game's 640-space (hud.cpp, flight mode): the top row is a strip of 0x30-high panel
// tiles - radar tile 0..0x80 (the radar circle reaches y 0x80), two empty tiles 0x80..0x180, the status
// bars 0x180..0x200 and the two spell-hand labels 0x1fe / 0x23e. The messages start at (0x84, 0x32) and
// stay with the centred frame. Widescreen: the radar tile goes to the top-left display corner, the
// status / hand tiles to the top-right, the middle tiles to the top edge. The tile seams are cut by
// design; the top-edge and right blocks move only when no HUD shape crosses their other edges (a long
// message, a pointer); the radar block always moves.
constexpr int kStripH = 0x30, kLeftX = 0x80, kLeftH = 0x80, kRightX = 0x180;

void build_blits(ComposeOutput &o, bool flight_view) {
    o.blit_count = 0;
    o.corners_anchored = false;
    const int gw = s_game_w, gh = s_game_h;
    const int shift_l = -s_hud_x, shift_r = s_disp_w - (s_hud_x + s_hud_w), shift_t = -s_hud_y;
    if (!(flight_view && g_settings.hud_corners && o.mask && (shift_l < 0 || shift_r > 0 || shift_t < 0))) {
        add_blit(o, 0, 0, gw, gh, 0, 0);
        return;
    }
    // 640-space -> frame pixels (320 wide: half; rows: the 400 / 480 virtual height)
    const int vh = gw == 320 ? 400 : 480;
    const int lx = kLeftX * gw / 640, rx = kRightX * gw / 640;
    const int strip = kStripH * gh / vh, lh = kLeftH * gh / vh;
    // The radar block always moves: the only shapes that cross its edges in flight are the radar's own
    // castle / balloon icons near the rim (messages start right of it), and keeping the whole block in
    // place for those frames made the radar jump sideways now and then. An overhanging icon is cut instead.
    const bool left_ok = true;
    const bool mid_ok = !crosses_row(strip, lx, rx);
    const bool right_ok = !crosses_row(strip, rx, gw);
    add_blit(o, 0, 0, lx, lh, left_ok ? shift_l : 0, left_ok ? shift_t : 0);
    add_blit(o, lx, 0, rx - lx, strip, 0, mid_ok ? shift_t : 0);
    add_blit(o, rx, 0, gw - rx, strip, right_ok ? shift_r : 0, right_ok ? shift_t : 0);
    add_blit(o, lx, strip, gw - lx, lh - strip, 0, 0);
    add_blit(o, 0, lh, gw, gh - lh, 0, 0);
    o.corners_anchored = left_ok || right_ok;
}

} // namespace

void compose_install() { s_installed = true; g_render_view_override = compose_view; }
void compose_remove() {
    s_installed = false;
    if (g_render_view_override == compose_view) g_render_view_override = nullptr;
    s_out = ComposeOutput{};
    s_pass = 0;
}
bool compose_installed() { return s_installed; }

void compose_hud_rect(int dw, int dh, int gw, int gh, int mode, int *x, int *y, int *w, int *h) {
    // fit: the 4:3 rectangle touching the display's short side
    int fw, fh;
    if ((int64_t)dw * 3 >= (int64_t)dh * 4) { fh = dh; fw = (int)(((int64_t)dh * 4 + 1) / 3); }
    else { fw = dw; fh = (int)(((int64_t)dw * 3 + 2) / 4); }
    if (fw > dw) fw = dw;
    if (fh > dh) fh = dh;
    int rw = fw, rh = fh;
    if (mode == 0 && gw > 0 && gh > 0) {
        // integer: whole display pixels per frame row; the column scale follows the 4:3 pixel aspect
        // (640x480: square, 320x200: 5/6) rounded to whole pixels as well
        const int sy = fh / gh;
        const int sx_num = sy * gh * 4, sx_den = 3 * gw;           // ideal column scale = sy * (gh*4/3) / gw
        int sx = (sx_num + sx_den / 2) / sx_den;
        while (sx > 0 && sx * gw > dw) sx--;
        // keep the shape: more than 5 % off the 4:3 pixel aspect (320x200 at 4x: 3 x 4 is 10 % off) -> fit
        const bool aspect_ok = sx >= 1 && std::abs((int64_t)sx * sx_den - (int64_t)sx_num) * 20 <= (int64_t)sx_num;
        if (sy >= 1 && aspect_ok) { rw = sx * gw; rh = sy * gh; }
    }
    *w = rw; *h = rh;
    *x = (dw - rw) / 2; *y = (dh - rh) / 2;
}

void compose_begin_frame(int dw, int dh, const FrameBuffer &fb) {
    s_disp_w = dw > 0 ? dw : 1; s_disp_h = dh > 0 ? dh : 1;
    s_game_w = fb.width; s_game_h = fb.height;
    compose_hud_rect(s_disp_w, s_disp_h, s_game_w, s_game_h, g_settings.hud_scale_mode, &s_hud_x, &s_hud_y, &s_hud_w, &s_hud_h);
    s_pass = 1;
    s_view_placed = s_view_placed2 = false;
}

bool compose_need_second_pass() { return s_pass == 1 && s_view_placed; }

void compose_begin_second_pass(const FrameBuffer &fb) {
    if (!compose_need_second_pass()) return;
    const size_t n = (size_t)s_game_w * s_game_h;
    s_final1.assign(fb.pixels, fb.pixels + n);
    s_mask.assign(n, 1);
    window_diff(fb, 0, false);
    s_pass = 2;
}

void compose_end_frame(const FrameBuffer &fb) {
    const size_t n = (size_t)s_game_w * s_game_h;
    ComposeOutput o;
    o.display_w = s_disp_w; o.display_h = s_disp_h;
    o.game_w = s_game_w; o.game_h = s_game_h;
    o.hud_x = s_hud_x; o.hud_y = s_hud_y; o.hud_w = s_hud_w; o.hud_h = s_hud_h;
    o.hud_filtered = g_settings.hud_scale_mode == 2;
    if (s_view_placed && fb.pixels && fb.width == s_game_w && fb.height == s_game_h) {
        if (s_pass == 2) {
            if (s_view_placed2) window_diff(fb, 0x80, true);
            std::memcpy(fb.pixels, s_final1.data(), n);     // the frame is pass 1's
        } else {
            s_mask.assign(n, 1);
            window_diff(fb, 0, false);
        }
        o.has_view = true;
        o.view = s_view.data();
        o.view_buf_w = s_vbw; o.view_buf_h = s_vbh;
        o.view_x = s_vx; o.view_y = s_vy; o.view_w = s_vw; o.view_h = s_vh;
        o.mask = s_mask.data();
    }
    if (fb.pixels) s_hud.assign(fb.pixels, fb.pixels + n); else s_hud.assign(n, 0);
    o.hud = s_hud.data();
    const bool flight_view = o.has_view && s_vx == 0 && s_vy == 0 && s_vw == s_disp_w && s_vh == s_disp_h;
    build_blits(o, flight_view);
    s_out = o;
    s_pass = 0;
    s_fresh = true;
}

const ComposeOutput &compose_output() { return s_out; }

const ComposeOutput &compose_frame_for_present(int dw, int dh, const FrameBuffer &fb) {
    if (s_fresh) { s_fresh = false; return s_out; }
    const size_t n = (size_t)fb.width * fb.height;
    const bool same = fb.pixels && s_out.hud && s_out.game_w == fb.width && s_out.game_h == fb.height &&
                      std::memcmp(fb.pixels, s_out.hud, n) == 0;
    if (!same && fb.pixels) {
        compose_begin_frame(dw, dh, fb);
        compose_end_frame(fb);
        s_fresh = false;
    }
    return s_out;
}

void compose_display_to_frame(const ComposeOutput &o, int dx, int dy, int *fx, int *fy) {
    if (o.hud_w <= 0 || o.hud_h <= 0 || o.game_w <= 0 || o.game_h <= 0) { *fx = dx; *fy = dy; return; }
    int x = (int)floordiv((int64_t)(dx - o.hud_x) * o.game_w, o.hud_w);
    int y = (int)floordiv((int64_t)(dy - o.hud_y) * o.game_h, o.hud_h);
    *fx = x < 0 ? 0 : x >= o.game_w ? o.game_w - 1 : x;
    *fy = y < 0 ? 0 : y >= o.game_h ? o.game_h - 1 : y;
}

void compose_frame_to_display(const ComposeOutput &o, int fx, int fy, int *dx, int *dy) {
    if (o.hud_w <= 0 || o.hud_h <= 0 || o.game_w <= 0 || o.game_h <= 0) { *dx = fx; *dy = fy; return; }
    *dx = o.hud_x + (int)floordiv((int64_t)(2 * fx + 1) * o.hud_w, 2 * (int64_t)o.game_w);
    *dy = o.hud_y + (int)floordiv((int64_t)(2 * fy + 1) * o.hud_h, 2 * (int64_t)o.game_h);
}

bool compose_display_to_view(const ComposeOutput &o, int dx, int dy, int *vx, int *vy) {
    if (!o.has_view || o.view_w <= 0 || o.view_h <= 0 || o.view_buf_w <= 0 || o.view_buf_h <= 0) return false;
    if (dx < o.view_x || dy < o.view_y || dx >= o.view_x + o.view_w || dy >= o.view_y + o.view_h) return false;
    *vx = (int)((int64_t)(dx - o.view_x) * o.view_buf_w / o.view_w);    // compose_to_rgb's sampling
    *vy = (int)((int64_t)(dy - o.view_y) * o.view_buf_h / o.view_h);
    return true;
}

void compose_view_to_display(const ComposeOutput &o, double vx, double vy, double *dx, double *dy) {
    const double sx = o.view_buf_w > 0 ? (double)o.view_w / o.view_buf_w : 1.0;
    const double sy = o.view_buf_h > 0 ? (double)o.view_h / o.view_buf_h : 1.0;
    *dx = o.view_x + (vx + 0.5) * sx;
    *dy = o.view_y + (vy + 0.5) * sy;
}

void compose_to_rgb(const ComposeOutput &o, const uint8_t *pal6, uint8_t *out) {
    uint8_t pal[768];
    for (int i = 0; i < 768; ++i) pal[i] = (uint8_t)((pal6[i] << 2) | (pal6[i] >> 4));
    const int W = o.display_w, H = o.display_h;
    std::memset(out, 0, (size_t)W * H * 3);
    if (o.has_view && o.view) {
        for (int y = 0; y < o.view_h; ++y) {
            const int Y = o.view_y + y;
            if (Y < 0 || Y >= H) continue;
            const uint8_t *src = o.view + (size_t)((int64_t)y * o.view_buf_h / o.view_h) * o.view_buf_w;
            uint8_t *d = out + ((size_t)Y * W) * 3;
            for (int x = 0; x < o.view_w; ++x) {
                const int X = o.view_x + x;
                if (X < 0 || X >= W) continue;
                std::memcpy(d + (size_t)X * 3, pal + src[(int64_t)x * o.view_buf_w / o.view_w] * 3, 3);
            }
        }
    }
    if (!o.hud) return;
    for (int i = 0; i < o.blit_count; ++i) {
        const ComposeBlit &b = o.blits[i];
        for (int y = 0; y < b.dh; ++y) {
            const int Y = b.dy + y;
            if (Y < 0 || Y >= H) continue;
            const int sy = b.sy + (int)((int64_t)y * b.sh / b.dh);
            const uint8_t *src = o.hud + (size_t)sy * o.game_w;
            const uint8_t *m = o.mask ? o.mask + (size_t)sy * o.game_w : nullptr;
            uint8_t *d = out + ((size_t)Y * W) * 3;
            for (int x = 0; x < b.dw; ++x) {
                const int X = b.dx + x;
                if (X < 0 || X >= W) continue;
                const int sx = b.sx + (int)((int64_t)x * b.sw / b.dw);
                if (m && !m[sx]) continue;
                std::memcpy(d + (size_t)X * 3, pal + src[sx] * 3, 3);
            }
        }
    }
}
