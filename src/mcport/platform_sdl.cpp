#include "platform.h"
#include "compose.h"
#include <SDL.h>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace mc {

// ---------------------------------------------------------------------------------------------
// Palette conversion workers: the composed present converts up to 8.3 Mpixels (4K) of palette indices
// to ARGB every frame; rows are split over a small persistent pool (the calling thread takes a share).
// ---------------------------------------------------------------------------------------------
namespace {

struct ConvertJob {
    const uint8_t *src = nullptr; int src_pitch = 0;
    uint8_t *dst = nullptr; int dst_pitch = 0;
    int w = 0, h = 0;
    const uint32_t *pal = nullptr;
    const uint8_t *mask = nullptr;   // HUD layer: premultiplied ARGB, 0 where the mask is 0
};

void convert_rows(const ConvertJob &j, int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        const uint8_t *s = j.src + (size_t)y * j.src_pitch;
        uint32_t *d = reinterpret_cast<uint32_t *>(j.dst + (size_t)y * j.dst_pitch);
        if (j.mask) {
            const uint8_t *m = j.mask + (size_t)y * j.src_pitch;
            for (int x = 0; x < j.w; ++x) d[x] = m[x] ? j.pal[s[x]] : 0u;
        } else {
            for (int x = 0; x < j.w; ++x) d[x] = j.pal[s[x]];
        }
    }
}

class ConvertPool {
public:
    ConvertPool() {
        unsigned n = std::thread::hardware_concurrency();
        n = n > 1 ? std::min(n - 1, 7u) : 0;
        for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this, i] { worker(i); });
    }
    ~ConvertPool() {
        { std::lock_guard<std::mutex> l(m_); quit_ = true; }
        cv_.notify_all();
        for (auto &t : threads_) t.join();
    }
    void run(const ConvertJob &j) {
        const int parts = (int)threads_.size() + 1;
        if (threads_.empty() || (int64_t)j.w * j.h < 200000) { convert_rows(j, 0, j.h); return; }
        {
            std::lock_guard<std::mutex> l(m_);
            job_ = j; parts_ = parts; pending_ = (int)threads_.size(); gen_++;
        }
        cv_.notify_all();
        convert_rows(j, (int)((int64_t)j.h * (parts - 1) / parts), j.h);   // our share: the last part
        std::unique_lock<std::mutex> l(m_);
        done_.wait(l, [this] { return pending_ == 0; });
    }
private:
    void worker(unsigned idx) {
        uint64_t seen = 0;
        for (;;) {
            ConvertJob j; int parts;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [&] { return quit_ || gen_ != seen; });
                if (quit_) return;
                seen = gen_; j = job_; parts = parts_;
            }
            convert_rows(j, (int)((int64_t)j.h * idx / parts), (int)((int64_t)j.h * (idx + 1) / parts));
            {
                std::lock_guard<std::mutex> l(m_);
                if (--pending_ == 0) done_.notify_one();
            }
        }
    }
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    ConvertJob job_;
    int parts_ = 1, pending_ = 0;
    uint64_t gen_ = 0;
    bool quit_ = false;
};

ConvertPool *s_pool = nullptr;

} // namespace

bool Platform::init(const char *title, int w, int h, int scale) {
    // Direct3D 11 presents the composed frame fastest here (docs/analysis/port_compose.md: 1080p 2.5 ms,
    // 4K 7.2 ms per frame vs 4.1 / 9.4 ms with SDL's default Direct3D 9); SDL_RENDER_DRIVER overrides.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");   // a hint: ignored where unavailable
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) return false;
    fb_w = w; fb_h = h;
    fb = new uint8_t[w * h]();
    rgba_ = new uint32_t[w * h]();
    SDL_Window *win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       w * scale, h * scale, SDL_WINDOW_RESIZABLE);
    if (!win) return false;
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {                                   // the preferred driver is missing: any accelerated one
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);   // e.g. SDL_VIDEODRIVER=dummy
    if (!ren) return false;
    SDL_RenderSetLogicalSize(ren, w, h);
    SDL_RenderSetIntegerScale(ren, SDL_FALSE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!tex) return false;
    window_ = win; renderer_ = ren; texture_ = tex;
    logical_ = true;
    SDL_RendererInfo info;
    vsync_ = SDL_GetRendererInfo(ren, &info) == 0 && (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    // The HUD layer is premultiplied (transparent = 0): dst = src + dst * (1 - src alpha), so linear
    // filtering (hud_scale_mode 2) blends edges correctly. Fall back to plain alpha blending.
    const SDL_BlendMode pm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                                        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    hud_blend_ = (int)pm;
    if (!s_pool) s_pool = new ConvertPool();
    return true;
}

void Platform::shutdown() {
    if (view_tex_) SDL_DestroyTexture((SDL_Texture *)view_tex_);
    if (hud_tex_) SDL_DestroyTexture((SDL_Texture *)hud_tex_);
    view_tex_ = hud_tex_ = nullptr;
    if (texture_) SDL_DestroyTexture((SDL_Texture *)texture_);
    if (renderer_) SDL_DestroyRenderer((SDL_Renderer *)renderer_);
    if (window_) SDL_DestroyWindow((SDL_Window *)window_);
    texture_ = renderer_ = window_ = nullptr;
    delete[] fb; delete[] rgba_;
    fb = nullptr; rgba_ = nullptr;
    delete s_pool; s_pool = nullptr;
    SDL_Quit();
}

void Platform::set_palette(const uint8_t *rgb) {
    for (int i = 0; i < 256; i++)
        palette_[i] = 0xFF000000u | ((uint32_t)rgb[i * 3] << 16) | ((uint32_t)rgb[i * 3 + 1] << 8) | rgb[i * 3 + 2];
}

void Platform::use_logical_size(bool on) {
    if (on == logical_) return;
    SDL_RenderSetLogicalSize((SDL_Renderer *)renderer_, on ? fb_w : 0, on ? fb_h : 0);
    if (!on) SDL_RenderSetScale((SDL_Renderer *)renderer_, 1.0f, 1.0f);
    SDL_RenderSetViewport((SDL_Renderer *)renderer_, nullptr);
    logical_ = on;
}

void Platform::present() {
    const uint64_t t0 = ticks_us();
    use_logical_size(true);
    composed_ = false;
    const int n = fb_w * fb_h;
    for (int i = 0; i < n; i++) rgba_[i] = palette_[fb[i]];
    SDL_UpdateTexture((SDL_Texture *)texture_, nullptr, rgba_, fb_w * 4);
    SDL_RenderClear((SDL_Renderer *)renderer_);
    SDL_RenderCopy((SDL_Renderer *)renderer_, (SDL_Texture *)texture_, nullptr, nullptr);
    last_convert_us_ = 0;
    last_present_us_ = ticks_us() - t0;
    SDL_RenderPresent((SDL_Renderer *)renderer_);
}

// (Re)create a streaming ARGB texture of w x h.
static SDL_Texture *ensure_texture(SDL_Renderer *ren, void *&tex, int &tw, int &th, int w, int h) {
    if (tex && tw == w && th == h) return (SDL_Texture *)tex;
    if (tex) SDL_DestroyTexture((SDL_Texture *)tex);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    tw = tex ? w : 0; th = tex ? h : 0;
    if (tex) { SDL_SetTextureScaleMode((SDL_Texture *)tex, SDL_ScaleModeNearest); SDL_SetTextureBlendMode((SDL_Texture *)tex, SDL_BLENDMODE_NONE); }
    return (SDL_Texture *)tex;
}

// Palette conversion straight into a locked streaming texture.
static bool upload(SDL_Texture *tex, const uint8_t *src, const uint8_t *mask, int w, int h, const uint32_t *pal) {
    void *pixels; int pitch;
    if (SDL_LockTexture(tex, nullptr, &pixels, &pitch) != 0) return false;
    ConvertJob j;
    j.src = src; j.src_pitch = w; j.dst = (uint8_t *)pixels; j.dst_pitch = pitch; j.w = w; j.h = h; j.pal = pal; j.mask = mask;
    if (s_pool) s_pool->run(j); else convert_rows(j, 0, h);
    SDL_UnlockTexture(tex);
    return true;
}

void Platform::present_composed(const ComposeOutput &o) {
    if (!o.hud || o.game_w <= 0 || o.game_h <= 0) { present(); return; }
    const uint64_t t0 = ticks_us();
    SDL_Renderer *ren = (SDL_Renderer *)renderer_;
    use_logical_size(false);
    composed_ = true;
    map_game_w_ = o.game_w; map_game_h_ = o.game_h;
    map_hud_x_ = o.hud_x; map_hud_y_ = o.hud_y;
    map_hud_w_ = o.hud_w > 0 ? o.hud_w : 1; map_hud_h_ = o.hud_h > 0 ? o.hud_h : 1;

    // The layout was made for the drawable size at compose_begin_frame; if the window changed since,
    // scale it (one frame).
    int ow, oh;
    output_size(&ow, &oh);
    const float sx = o.display_w > 0 ? (float)ow / (float)o.display_w : 1.0f;
    const float sy = o.display_h > 0 ? (float)oh / (float)o.display_h : 1.0f;
    auto rect = [&](int x, int y, int w, int h) {
        SDL_Rect r;
        r.x = (int)((float)x * sx); r.y = (int)((float)y * sy);
        r.w = (int)((float)(x + w) * sx) - r.x; r.h = (int)((float)(y + h) * sy) - r.y;
        return r;
    };

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    uint64_t conv = 0;
    if (o.has_view && o.view && o.view_buf_w > 0 && o.view_buf_h > 0) {
        if (SDL_Texture *vt = ensure_texture(ren, view_tex_, view_tex_w_, view_tex_h_, o.view_buf_w, o.view_buf_h)) {
            const uint64_t c0 = ticks_us();
            upload(vt, o.view, nullptr, o.view_buf_w, o.view_buf_h, palette_);
            conv += ticks_us() - c0;
            const SDL_Rect d = rect(o.view_x, o.view_y, o.view_w, o.view_h);
            SDL_RenderCopy(ren, vt, nullptr, &d);
        }
    }
    if (SDL_Texture *ht = ensure_texture(ren, hud_tex_, hud_tex_w_, hud_tex_h_, o.game_w, o.game_h)) {
        // (set every frame: a recreated texture starts with nearest)
        SDL_SetTextureScaleMode(ht, o.hud_filtered ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        const uint64_t c0 = ticks_us();
        upload(ht, o.hud, o.mask, o.game_w, o.game_h, palette_);
        conv += ticks_us() - c0;
        if (o.mask) {
            if (SDL_SetTextureBlendMode(ht, (SDL_BlendMode)hud_blend_) != 0) SDL_SetTextureBlendMode(ht, SDL_BLENDMODE_BLEND);
        } else {
            SDL_SetTextureBlendMode(ht, SDL_BLENDMODE_NONE);
        }
        for (int i = 0; i < o.blit_count; ++i) {
            const ComposeBlit &b = o.blits[i];
            const SDL_Rect s{b.sx, b.sy, b.sw, b.sh};
            const SDL_Rect d = rect(b.dx, b.dy, b.dw, b.dh);
            SDL_RenderCopy(ren, ht, &s, &d);
        }
    }
    last_convert_us_ = conv;
    last_present_us_ = ticks_us() - t0;
    SDL_RenderPresent(ren);
}

void Platform::output_size(int *w, int *h) const {
    if (SDL_GetRendererOutputSize((SDL_Renderer *)renderer_, w, h) != 0 || *w <= 0 || *h <= 0) {
        SDL_GetWindowSize((SDL_Window *)window_, w, h);
    }
}

void Platform::window_to_drawable(int wx, int wy, int *dx, int *dy) const {
    int ww, wh, ow, oh;
    SDL_GetWindowSize((SDL_Window *)window_, &ww, &wh);
    output_size(&ow, &oh);
    *dx = ww > 0 ? (int)((int64_t)wx * ow / ww) : wx;
    *dy = wh > 0 ? (int)((int64_t)wy * oh / wh) : wy;
}

void Platform::set_fullscreen(bool on) {
    if (!window_) return;
    if (SDL_SetWindowFullscreen((SDL_Window *)window_, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0) fullscreen_ = on;
}

bool Platform::set_vsync(bool on) {
    if (!renderer_) return false;
    if (SDL_RenderSetVSync((SDL_Renderer *)renderer_, on ? 1 : 0) != 0) return false;
    vsync_ = on;
    return true;
}

void Platform::poll(Input &in) {
    std::memset(in.key_pressed, 0, sizeof in.key_pressed);
    in.mouse_dx = in.mouse_dy = 0;
    in.key_event_count = 0;
    in.wheel = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: in.quit = true; break;
        case SDL_MOUSEWHEEL: in.wheel += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y; break;
        case SDL_KEYDOWN:
            if ((e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
                (e.key.keysym.mod & KMOD_ALT)) {
                if (!e.key.repeat) toggle_fullscreen();      // Alt+Enter: not passed to the game
                break;
            }
            if (in.key_event_count < 64) in.key_events[in.key_event_count++] = { (int)e.key.keysym.scancode, true };
            if (e.key.keysym.scancode < 512) {
                if (!in.keys[e.key.keysym.scancode]) in.key_pressed[e.key.keysym.scancode] = true;
                in.keys[e.key.keysym.scancode] = true;
            }
            break;
        case SDL_KEYUP:
            if ((e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
                !in.keys[e.key.keysym.scancode])
                break;                                       // the release of a swallowed Alt+Enter
            if (in.key_event_count < 64) in.key_events[in.key_event_count++] = { (int)e.key.keysym.scancode, false };
            if (e.key.keysym.scancode < 512) in.keys[e.key.keysym.scancode] = false;
            break;
        case SDL_MOUSEMOTION: {
            if (composed_) {
                // window -> drawable -> game frame through the HUD rectangle (compose.h); the relative
                // motion scaled to game-frame pixels as the logical-size path delivers it
                int dx, dy;
                window_to_drawable(e.motion.x, e.motion.y, &dx, &dy);
                in.display_x = dx; in.display_y = dy;          // round 10 (task C): display pointer
                ComposeOutput m;
                m.game_w = map_game_w_; m.game_h = map_game_h_;
                m.hud_x = map_hud_x_; m.hud_y = map_hud_y_; m.hud_w = map_hud_w_; m.hud_h = map_hud_h_;
                compose_display_to_frame(m, dx, dy, &in.mouse_x, &in.mouse_y);
                int rx, ry;
                window_to_drawable(e.motion.xrel, e.motion.yrel, &rx, &ry);
                const int64_t nx = (int64_t)rx * map_game_w_ + rel_rem_x_, ny = (int64_t)ry * map_game_h_ + rel_rem_y_;
                in.mouse_dx += (int)(nx / map_hud_w_); rel_rem_x_ = (int)(nx % map_hud_w_);
                in.mouse_dy += (int)(ny / map_hud_h_); rel_rem_y_ = (int)(ny % map_hud_h_);
                break;
            }
            // With SDL_RenderSetLogicalSize, SDL already delivers mouse events in logical (framebuffer)
            // coordinates: converting them again squeezed the pointer into the window's top-left.
            in.mouse_x = e.motion.x < 0 ? 0 : (e.motion.x >= fb_w ? fb_w - 1 : e.motion.x);
            in.mouse_y = e.motion.y < 0 ? 0 : (e.motion.y >= fb_h ? fb_h - 1 : e.motion.y);
            {   // round 10 (task C): display pointer - the logical position back to the window, then drawable
                int wx = e.motion.x, wy = e.motion.y;
                if (logical_) SDL_RenderLogicalToWindow((SDL_Renderer *)renderer_, (float)e.motion.x + 0.5f, (float)e.motion.y + 0.5f, &wx, &wy);
                window_to_drawable(wx, wy, &in.display_x, &in.display_y);
            }
            in.mouse_dx += e.motion.xrel; in.mouse_dy += e.motion.yrel;
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            bool down = e.type == SDL_MOUSEBUTTONDOWN;
            if (e.button.button == SDL_BUTTON_LEFT) in.mouse_l = down;
            if (e.button.button == SDL_BUTTON_RIGHT) in.mouse_r = down;
            if (e.button.button == SDL_BUTTON_MIDDLE) in.mouse_m = down;
            break;
        }
        }
    }
}

bool Platform::relative_mouse() const { return SDL_GetRelativeMouseMode() == SDL_TRUE; }

void Platform::refresh_pointer(Input &in) const {
    if (!window_) return;
    int wx, wy;
    SDL_GetMouseState(&wx, &wy);            // window coordinates in both presentations
    window_to_drawable(wx, wy, &in.display_x, &in.display_y);
}

void Platform::display_to_frame(int dx, int dy, int *fx, int *fy) const {
    if (composed_) {
        ComposeOutput m;
        m.game_w = map_game_w_; m.game_h = map_game_h_;
        m.hud_x = map_hud_x_; m.hud_y = map_hud_y_; m.hud_w = map_hud_w_; m.hud_h = map_hud_h_;
        compose_display_to_frame(m, dx, dy, fx, fy);
        return;
    }
    int ww, wh, ow, oh;
    SDL_GetWindowSize((SDL_Window *)window_, &ww, &wh);
    output_size(&ow, &oh);
    const int wx = ow > 0 ? (int)((int64_t)dx * ww / ow) : dx, wy = oh > 0 ? (int)((int64_t)dy * wh / oh) : dy;
    float lx = (float)wx, ly = (float)wy;
    if (renderer_) SDL_RenderWindowToLogical((SDL_Renderer *)renderer_, wx, wy, &lx, &ly);
    const int x = (int)lx, y = (int)ly;
    *fx = x < 0 ? 0 : x >= fb_w ? fb_w - 1 : x;
    *fy = y < 0 ? 0 : y >= fb_h ? fb_h - 1 : y;
}

void Platform::set_relative_mouse(bool on) { SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE); }
void Platform::warp_mouse(int fb_x, int fb_y) {
    int wx, wy;
    if (composed_) {
        ComposeOutput m;
        m.game_w = map_game_w_; m.game_h = map_game_h_;
        m.hud_x = map_hud_x_; m.hud_y = map_hud_y_; m.hud_w = map_hud_w_; m.hud_h = map_hud_h_;
        int dx, dy;
        compose_frame_to_display(m, fb_x, fb_y, &dx, &dy);
        int ww, wh, ow, oh;
        SDL_GetWindowSize((SDL_Window *)window_, &ww, &wh);
        output_size(&ow, &oh);
        wx = ow > 0 ? (int)((int64_t)dx * ww / ow) : dx;
        wy = oh > 0 ? (int)((int64_t)dy * wh / oh) : dy;
    } else {
        SDL_RenderLogicalToWindow((SDL_Renderer *)renderer_, (float)fb_x, (float)fb_y, &wx, &wy);
    }
    SDL_WarpMouseInWindow((SDL_Window *)window_, wx, wy);
}
bool Platform::resize(int w, int h) {
    if (w == fb_w && h == fb_h) return true;
    SDL_Texture *tex = SDL_CreateTexture((SDL_Renderer *)renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!tex) return false;
    SDL_DestroyTexture((SDL_Texture *)texture_);
    texture_ = tex;
    delete[] fb; delete[] rgba_;
    fb_w = w; fb_h = h;
    fb = new uint8_t[w * h]();
    rgba_ = new uint32_t[w * h]();
    if (logical_) SDL_RenderSetLogicalSize((SDL_Renderer *)renderer_, w, h);
    // the composed mouse mapping follows the new frame size until the next composed present
    if (composed_) { map_game_w_ = w; map_game_h_ = h; }
    return true;
}
void Platform::set_title(const char *title) { SDL_SetWindowTitle((SDL_Window *)window_, title); }
void Platform::set_window_size(int w, int h) {
    SDL_SetWindowSize((SDL_Window *)window_, w, h);
    SDL_SetWindowPosition((SDL_Window *)window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}
uint32_t Platform::ticks_ms() const { return SDL_GetTicks(); }
uint64_t Platform::ticks_us() const {
    return SDL_GetPerformanceCounter() * 1000000ull / SDL_GetPerformanceFrequency();
}
void Platform::sleep_ms(uint32_t ms) const { SDL_Delay(ms); }

} // namespace mc
