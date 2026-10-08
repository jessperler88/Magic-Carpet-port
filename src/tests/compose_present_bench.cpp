#define _CRT_SECURE_NO_WARNINGS
// Timing of the composed presentation (platform_sdl.cpp Platform::present_composed, round 7 task B):
// a hidden window per display size (1920x1080, 2560x1440, 3840x2160, or argv[2]="WxH"), a synthetic
// 8-bit view of the display size and a 640x480 HUD layer with a mask; reports the CPU time of the
// present call (palette conversion into the streaming textures + render copies) averaged over N frames,
// and the wall time per frame including SDL_RenderPresent with vsync off. Not a pass / fail test: exit 0
// unless SDL cannot create a renderer (then SKIP).
#include "platform.h"
#include "compose.h"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    struct Size { int w, h; };
    std::vector<Size> sizes = {{1920, 1080}, {2560, 1440}, {3840, 2160}};
    if (argc > 2) { int w, h; if (std::sscanf(argv[2], "%dx%d", &w, &h) == 2) sizes = {{w, h}}; }
    const int frames = argc > 3 ? std::atoi(argv[3]) : 120;
    for (const Size &sz : sizes) {
        mc::Platform plat;
        // the window at the display size (scale 1 over a frame of that size; then shrink the frame)
        if (!plat.init("compose_present_bench", sz.w, sz.h, 1)) { std::printf("SKIP: SDL init failed: %s\n", SDL_GetError()); return 0; }
        plat.resize(640, 480);
        plat.set_vsync(false);
        uint8_t rgb[768];
        for (int i = 0; i < 256; i++) { rgb[i * 3] = (uint8_t)i; rgb[i * 3 + 1] = (uint8_t)(255 - i); rgb[i * 3 + 2] = (uint8_t)(i * 7); }
        plat.set_palette(rgb);
        int dw, dh;
        plat.output_size(&dw, &dh);
        std::vector<uint8_t> view((size_t)dw * dh), hud(640 * 480), mask(640 * 480, 0);
        for (size_t i = 0; i < view.size(); i++) view[i] = (uint8_t)(i * 2654435761u >> 24);
        for (int y = 0; y < 480; y++)
            for (int x = 0; x < 640; x++) { hud[(size_t)y * 640 + x] = (uint8_t)(x ^ y); mask[(size_t)y * 640 + x] = y < 48 || x < 128; }
        ComposeOutput o;
        o.display_w = dw; o.display_h = dh; o.game_w = 640; o.game_h = 480;
        compose_hud_rect(dw, dh, 640, 480, 0, &o.hud_x, &o.hud_y, &o.hud_w, &o.hud_h);
        o.has_view = true; o.view = view.data(); o.view_buf_w = dw; o.view_buf_h = dh;
        o.view_x = 0; o.view_y = 0; o.view_w = dw; o.view_h = dh;
        o.hud = hud.data(); o.mask = mask.data();
        o.blits[0] = ComposeBlit{0, 0, 640, 480, o.hud_x, o.hud_y, o.hud_w, o.hud_h}; o.blit_count = 1;
        for (int i = 0; i < 10; i++) plat.present_composed(o);          // warm-up
        uint64_t cpu = 0, conv = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; i++) {
            view[(size_t)i] ^= 1;
            plat.present_composed(o);
            cpu += plat.last_present_us(); conv += plat.last_convert_us();
        }
        const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
        uint64_t fcpu = 0;
        for (int i = 0; i < frames; i++) { plat.present(); fcpu += plat.last_present_us(); }
        SDL_RendererInfo info{};
        for (Uint32 id = 1; id < 64; id++)
            if (SDL_Window *w = SDL_GetWindowFromID(id)) { SDL_GetRendererInfo(SDL_GetRenderer(w), &info); break; }
        std::printf("%dx%d (%s, %u threads): composed present %.2f ms cpu (palette conversion %.2f ms), %.2f ms per frame incl. RenderPresent; "
                    "original present (640x480) %.2f ms cpu\n",
                    dw, dh, info.name ? info.name : "?", std::thread::hardware_concurrency(), cpu / 1000.0 / frames, conv / 1000.0 / frames, wall,
                    fcpu / 1000.0 / frames);
        plat.shutdown();
    }
    return 0;
}
