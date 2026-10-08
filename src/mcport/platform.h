/* Minimal platform layer: an 8-bit paletted framebuffer presented through SDL2.
 * This stands in for the DOS VGA/VESA mode-13h / mode-X surface the original
 * renders into; game code keeps drawing palette indices into `fb`.
 *
 * Two presentations (round 7, docs/analysis/port_compose.md):
 *  - present(): the original's - the game frame scaled to the window (nearest, SDL logical size);
 *  - present_composed(): the native mode - a hi-res 8-bit 3D view plus the game frame as an 8-bit HUD
 *    layer with a mask (compose.h ComposeOutput), palette applied at the end, drawn at the window's
 *    drawable resolution. While the composed presentation is in use, mouse positions in Input are
 *    still game-frame pixels (mapped through the HUD rectangle) and the relative motion is scaled to
 *    game-frame pixels, so callers need no change. */
#pragma once
#include <cstdint>

struct ComposeOutput;

namespace mc {

struct Input {
    bool quit = false;
    bool keys[512] = {};
    bool key_pressed[512] = {};   // edge-triggered this frame
    int mouse_x = 0, mouse_y = 0; // logical (framebuffer) coordinates
    int mouse_dx = 0, mouse_dy = 0; // relative motion since the last poll (framebuffer pixels)
    bool mouse_l = false, mouse_r = false;
    int wheel = 0;                // mouse wheel steps since the last poll (+ = away from the user)
    // Raw key transitions of this poll in arrival order, auto-repeat included (the game's own input
    // code wants them as the keyboard sends them). Alt+Enter (fullscreen toggle) is not reported.
    struct KeyEvent { int scancode; bool down; };
    KeyEvent key_events[64];
    int key_event_count = 0;
    // ---- round 10 (task C): the pointer at display resolution ----
    // The pointer in drawable (display) pixels, from the motion events in both presentations (composed: full
    // display precision; the original presentation: the logical pixel's centre mapped back), meaningful with
    // the absolute pointer (in relative mode SDL keeps the hidden pointer anywhere). Platform::refresh_pointer
    // reads the OS pointer once (when a cursor mode starts). mouse_x / mouse_y above stay game-frame pixels.
    int display_x = 0, display_y = 0;
    bool mouse_m = false;         // middle button held
};

class Platform {
public:
    bool init(const char *title, int fb_w, int fb_h, int window_scale);
    void shutdown();
    void set_palette(const uint8_t *rgb8_768);
    void present();                 // upload fb -> texture -> window (the original's look)
    // The composed frame (compose.h): view + HUD layer + mask through the current palette.
    void present_composed(const ComposeOutput &o);
    void poll(Input &in);           // pump events (Alt+Enter toggles borderless fullscreen here)
    void set_relative_mouse(bool on);
    void warp_mouse(int fb_x, int fb_y);   // move the OS pointer to a framebuffer position
    void set_title(const char *title);
    void set_window_size(int w, int h);  // client size, centred (config [video] window_width / height)
    bool resize(int fb_w, int fb_h);    // new framebuffer size (contents cleared), same window
    uint32_t ticks_ms() const;      // monotonic milliseconds
    uint64_t ticks_us() const;      // monotonic microseconds (performance counter)
    void sleep_ms(uint32_t ms) const;

    // ---- round 7 ----
    void output_size(int *w, int *h) const;   // the window's drawable size in pixels (compose_begin_frame)
    void set_fullscreen(bool on);             // borderless fullscreen on the window's display
    bool fullscreen() const { return fullscreen_; }
    void toggle_fullscreen() { set_fullscreen(!fullscreen_); }
    bool set_vsync(bool on);                  // false if the renderer cannot change it
    bool vsync() const { return vsync_; }
    uint64_t last_present_us() const { return last_present_us_; }   // CPU time of the last present call
    uint64_t last_convert_us() const { return last_convert_us_; }   // ... of which palette conversion

    // ---- round 10 (task C): display pointer ----
    bool composed() const { return composed_; }   // the last present was present_composed
    bool relative_mouse() const;                  // SDL relative mouse mode on (the level's steering)
    void refresh_pointer(Input &in) const;        // in.display_x / y = where the OS pointer is now
    // A drawable (display) pixel -> game-frame pixel, clamped: through the HUD rectangle when composed, through
    // the logical-size scaling otherwise (the same mapping the mouse events get).
    void display_to_frame(int dx, int dy, int *fx, int *fy) const;

    uint8_t *fb = nullptr;          // fb_w * fb_h palette indices
    int fb_w = 0, fb_h = 0;

private:
    void use_logical_size(bool on);
    void window_to_drawable(int wx, int wy, int *dx, int *dy) const;

    void *window_ = nullptr;
    void *renderer_ = nullptr;
    void *texture_ = nullptr;
    void *view_tex_ = nullptr; int view_tex_w_ = 0, view_tex_h_ = 0;
    void *hud_tex_ = nullptr;  int hud_tex_w_ = 0, hud_tex_h_ = 0;
    int hud_blend_ = 0;             // SDL_BlendMode for the premultiplied HUD layer
    uint32_t palette_[256] = {};
    uint32_t *rgba_ = nullptr;
    bool logical_ = true;           // SDL_RenderSetLogicalSize active (the original presentation)
    bool composed_ = false;         // the last present was present_composed: mouse mapping below
    int map_game_w_ = 0, map_game_h_ = 0, map_hud_x_ = 0, map_hud_y_ = 0, map_hud_w_ = 1, map_hud_h_ = 1;
    int rel_rem_x_ = 0, rel_rem_y_ = 0;   // remainders of the scaled relative motion
    bool fullscreen_ = false;
    bool vsync_ = true;
    uint64_t last_present_us_ = 0, last_convert_us_ = 0;
};

} // namespace mc
