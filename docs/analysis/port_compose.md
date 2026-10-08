# Port round 7, task B: native-resolution frame composition and presentation

Phase 3 (docs/port/BRIEFING_round7.md task B). Port-only code; nothing here exists in carpet.exe, so there
are no original addresses to list. Files: `src/mcengine/compose.{h,cpp}` (new, no SDL),
`src/mcport/platform.h`, `src/mcport/platform_sdl.cpp`, `src/mcengine/settings.h` section B,
`src/tests/compose_test.{cpp,cmake}`, `src/tests/compose_present_bench.cpp` (timing tool, declared in
compose_test.cmake, not an add_test).

## What was built

### Composition (compose.h / compose.cpp)

The game keeps drawing its one 8-bit frame (320x200 / 640x480) exactly as before. In the composed
("native") mode:

1. **View override.** `compose_install()` sets `g_render_view_override` (render.h). When
   `render_frame_1fab0`'s drawing (hud.cpp `frame_pass` / `map_screen`) asks for the view, the override
   reads the view window that `render_set_view_window` / `render_set_view_window_top` just selected
   (`g_rt_dest - fb.pixels`, `g_rt_width`, `g_rt_height`) and renders the 3D view with task A's
   `render_view_ext` into a **hi-res 8-bit view buffer**:
   - window = whole frame (flight, view size 0x28): the buffer is the **whole display** (16:9 / 21:9 see
     more sideways, same vertical field of view - task A's contract);
   - smaller window (reduced view sizes, the spell book / map screen's top-right view): the buffer is the
     window's rectangle on the display (through the HUD mapping below), so those views are hi-res too.
   - `g_settings.view_width / view_height` > 0 render at a lower resolution and let the presentation scale
     it (performance knob).
2. **Downsample.** The view buffer is point-sampled into the game frame's view window: game pixel centre
   -> display -> view buffer. The 2D pass then draws over a matching picture (translucent panels blend
   with what is under them). Point sampling, not averaging: averaging palette indices is meaningless and a
   palette-space average needs a colour search per pixel; the downsampled frame only feeds the translucent
   HUD blends (and the faithful-looking frame for screenshots).
3. **HUD diff, two passes.** After the 2D pass (render_frame_draw + debug overlay + pointer) the window is
   compared with what was placed: changed pixels are the **HUD layer**; every pixel outside the window is
   HUD (opaque). A single diff misses HUD pixels whose colour equals the view pixel under them (text over
   uniform sky, opaque sprites over flat ground) - they would become holes. `compose_draw_frame` therefore
   runs the 2D pass a second time over an altered view (every index `^ 0x80`): a pixel unchanged in both
   passes is the view (an opaque HUD pixel cannot equal both `v` and `v ^ 0x80`; a translucent one that
   maps both to themselves is invisible anyway). The second pass's pixels are thrown away (the frame is
   restored to pass 1's), only its diff is OR-ed in. The 2D pass is cheap (render_frame_draw without the
   view: well under a millisecond) and has no state writes (`render_frame_draw` is the draw-only half).
   The second pass runs only when the first placed a view.
4. **Layout / presentation.** The game frame is shown at 4:3 (320x200 as on a CRT, pixel aspect 5:6).
   `hud_scale_mode` 0 = integer: the largest whole row scale that fits, the column scale the 4:3 pixel
   aspect rounded to whole pixels (640x480 on 1080p: 2x -> 1280x960; on 1440p 3x = exact fit; on 4K 4x ->
   2560x1920; 320x200 on 1080p: 4 x 5 -> 1280x1000, aspect 1.28 instead of 1.33); falls back to fit when
   1x does not fit or the rounded column scale is more than 5 % off the aspect (320x200 at 3 x 4). 1 = exact 4:3 fit, nearest. 2 = fit, linear filtering (GPU). The rectangle is centred.
   Presentation = black, the hi-res view in its rectangle, the HUD layer on top through a list of blits
   (`ComposeOutput.blits`, each a source rectangle of the frame and its display rectangle), palette applied
   last - palette fades / flashes work unchanged because everything stays 8-bit until the final lookup.
5. **Corner anchoring (widescreen flight).** The flight HUD's top row is a strip of 0x30-high panel tiles:
   radar tile 0..0x80 (the radar circle reaches y 0x80), two empty tiles 0x80..0x180, status bars
   0x180..0x200, spell-hand labels 0x200..0x280 (640-space). With `hud_corners` (default on) and a
   full-display view wider (or taller, integer mode) than the HUD rectangle, the frame is cut into 5 blits:
   radar tile -> top-left display corner, status + hand tiles -> top-right corner, middle tiles -> top edge
   (centred), the rest (messages at 0x84,0x32, everything below) stays with the centred frame. The tile
   seams are cut by design; a block only moves when no HUD shape crosses its other edges (checked on the
   mask every frame: e.g. a message line running under the radar tile or the pointer on a boundary keeps
   that block in place for that frame). Full-screen 2D screens (spell book, map, help) and frames without a
   view are never split: 4:3 centred, black bars.
6. **No view this frame** (front end, title / result screens, FLI, anything drawn outside
   `compose_draw_frame`): `compose_frame_for_present` returns the frame as one opaque blit, pillarboxed.
   During a palette fade (nothing is drawn) it re-presents the last composed frame as long as the frame
   buffer still holds it (memcmp), so a level fading out keeps its hi-res view.
7. **Mouse.** `compose_display_to_frame` / `compose_frame_to_display` map display (drawable) pixels to
   game-frame pixels through the HUD rectangle (the central 4:3 area; clamped) and back (pixel centre).
   The platform applies them itself, so `Input.mouse_x/y` stay game-frame pixels and `mouse_dx/dy` are
   scaled to game-frame pixels (with remainders) - main.cpp's existing `* 640 / fb_w` conversions and the
   level's relative steering keep their feel at any window size. `warp_mouse` maps back.

With the override not installed, nothing of this runs: `render_view_frame` calls `render_view`, and
`present()` is today's path (logical size = frame, nearest).

### Platform (platform.h / platform_sdl.cpp)

- `present_composed(const ComposeOutput &)`: SDL_Renderer with two streaming ARGB textures - the view
  (view buffer size, nearest, no blending) and the HUD layer (frame size, premultiplied alpha:
  mask 0 -> 0x00000000, custom blend ONE / ONE_MINUS_SRC_ALPHA so linear filtering in mode 2 does not
  darken edges; falls back to SDL_BLENDMODE_BLEND). The palette conversion writes straight into the locked
  textures and is split over a persistent worker pool (hardware threads - 1, max 7, plus the caller; used
  above 200k pixels). Logical size is switched off while composed and restored by `present()`.
- `output_size()` (drawable size for `compose_begin_frame`), `set_fullscreen / toggle_fullscreen /
  fullscreen()` (borderless desktop fullscreen), **Alt+Enter** handled in `poll` (swallowed: the game
  never sees that Enter, so the spell book does not toggle), `set_vsync()` (SDL_RenderSetVSync),
  `last_present_us / last_convert_us` (timing).
- The window was already resizable; the composed path re-reads the drawable size every frame (a frame
  composed for a stale size is scaled for one frame).
- Faithful `present()` unchanged apart from restoring the logical size if a composed present ran.

### Present timing (Release, this machine: 24 hardware threads, `compose_present_bench`)

Synthetic display-size view + 640x480 HUD layer with mask, vsync off, 120 frames, averaged. "cpu" =
the present call before `SDL_RenderPresent` (palette conversion into the locked textures + copies);
"frame" = wall time per frame including `SDL_RenderPresent`.

| display | Direct3D 11 (chosen) cpu / frame | SDL default Direct3D 9 cpu / frame | OpenGL frame | Direct3D 12 frame |
|---|---|---|---|---|
| 1920x1080 | 1.0 / 2.5 ms | 0.5 / 4.1 ms | 2.8 ms | 2.8 ms |
| 2560x1440 | 1.7 / 3.5-4.1 ms | 0.7 / 5.5 ms | 4.9 ms | 3.4 ms |
| 3840x2160 | 5.5-6.2 / 6.8-7.8 ms | 1.3 / 9.4 ms | 8.5 ms | 7.6 ms |

The original present (640x480 frame) is 0.2-0.4 ms. Converting into a private buffer + `SDL_UpdateTexture`
instead of writing into the locked texture was slower everywhere (1080p 2.6 ms, 4K 8.7 ms with D3D11) and
was dropped. `Platform::init` now asks for Direct3D 11 (`SDL_HINT_RENDER_DRIVER`, normal priority: the
`SDL_RENDER_DRIVER` environment variable still wins; if that driver cannot be created any accelerated
renderer is tried, then software). That also affects the faithful presentation, pixel for pixel the same
(nearest scaling of the frame). 4K costs ~7 ms per frame for presentation alone: fine for 60 fps, tight
for 144 Hz; a GPU palette lookup (upload 8-bit indices, a quarter of the bytes) is the next step if needed.

### Composition cost (Release, compose_test)

`compose_draw_frame` at 1920x1080 with the real HUD: 4.0 ms, at 2560x1080: 5.6 ms, map screen (512x384
view): 1.2 ms - dominated by task A's current `render_view_ext` **stub** (faithful renderer at <= 640x480,
nearest upscale into the display buffer). The compositor's own work (point-sampling the window, two 2D
passes, diffs, 300 KB copies) is a fraction of a millisecond. `compose_to_rgb` (reference CPU compositor,
single thread, screenshots / tests only) 10-14 ms per 1080p-class frame.

## Verification

- `compose_test` (mc_test, `src/tests/compose_test.cpp`, exit 0; frame part SKIPs without game data):
  - layout: HUD rectangles for 10 display / frame / mode combinations (integer 2x / 3x / 4x for 640x480,
    4 x 5 and 8 x 10 for 320x200, the 5 % aspect guard falling back to fit at 1280x800, letterboxing on a
    portrait display); mouse round trip frame -> display -> frame exact for every sampled pixel of every
    layout, display -> frame monotonic and clamped;
  - the reference compositor on a synthetic 8x6 frame (view through mask 0, HUD pixel 2x2, black bars
    without a view);
  - level 38 snapshot, 640x480 frame on 1920x1080: view buffer 1920x1080, **0 of 307,200 window pixels
    differ** from the hi-res view sampled at their display centres; HUD diff: an opaque block made of the
    view's own colours found **400 / 400** (second pass), a translucent `BLEND[0x40][dest]` block 400 / 400,
    no false HUD pixels elsewhere; reduced view size 0x20: view rect = the window's display rectangle
    (1024x768 at 448,156), everything outside the window HUD; `view_width/height` 960x540 render
    resolution; no view -> mask null, one blit; `compose_frame_for_present` (fresh / unchanged / changed
    frame); the frame after `compose_draw_frame` is pass 1's (no altered pixels left);
  - **faithful**: `render_frame_draw` without the compositor, after install + remove, and with the override
    installed but no frame open: identical frames;
  - composed frames with the real HUD written to the scratch dir and looked at
    (`<scratch>/round7_B/compose_*.png`): 640x480 and 320x200 frames on 1920x1080 and 2560x1080 (corner blocks
    at the display corners, messages with the centred frame), spell book on 2560x1440 and map screen on
    2560x1080 (hi-res small view top-right, pillarboxed), fit mode on 2560x1080. Blits cover the frame exactly
    once; corners anchored in every widescreen flight frame.
- **mcport with the integration** (the main.cpp code below applied to a scratch copy of the current
  main.cpp, `MC_COMPOSE=1` as a stand-in for the config, `SDL_VIDEODRIVER=dummy`, 1280x800 window):
  `play 38` composed flight frames (`<scratch>/round7_B/run/`), the spell book via an injected Enter, the
  front end pillarboxed at 4:3 with black bars, and an injected mouse motion to window (300,200) puts the
  front-end pointer exactly there (mapping through the HUD rectangle).
- **Reference gate** (built from the current tree in a scratch CMake project, see below): render_reference_test
  1396/1396, render_reference_hud 2734/2734, render_reference2_test 3334/3334, render_reference_options
  3634/3634, render_reference_fe_test 33/33 pixel-identical, hud_test OK, reference_test 0 divergences over
  1396 ticks. My files are not in those tests' source lists (compose.cpp is only in the mcengine library /
  compose_test); settings.h only gained defaulted fields.
- Build: zero warnings in compose.*, platform_sdl.cpp, compose_test.cpp, compose_present_bench.cpp (/W4).
  **Build-dir note:** for most of the round `build_B` could not build mcengine because of other agents'
  in-progress files (`demo.cpp` line 91: a backslash-n turned into a real newline inside a string literal;
  `savegame.cpp` referencing `g_spell_pickup_keep_flags`, which `spells.cpp` defined inside an anonymous
  namespace - link error). The Release numbers and the reference runs above come from a scratch CMake
  project (`<scratch>/round7_B/mcport_int`) compiling the then-current tree with a fixed copy of demo.cpp and
  a one-line stub for that global. Both were fixed by their owners before the end of my round: `build_B`
  (Debug) now builds `compose_test` (passes, same numbers), `compose_present_bench` and `mcport` with zero
  warnings in my files.

## Settings added (settings.h section B)

| field | default | range | meaning |
|---|---|---|---|
| `compose` | 0 | 0 / 1 | 0 = original presentation; 1 = native (mcport installs the compositor) |
| `view_width`, `view_height` | 0 | 0 or 64..7680 / 64..4320 | native: 3D render resolution, 0 = the window's |
| `hud_scale_mode` | 0 | 0..2 | 0 integer nearest, 1 fit nearest, 2 fit linear |
| `hud_corners` | true | bool | widescreen flight: radar / status tiles to the display corners |

Platform options for task E's config (`PlatformOptions`, not engine settings): `fullscreen` (bool,
default false; Alt+Enter toggles), `vsync` (bool, default true - the renderer is created with vsync).
Proposed play defaults (mcport.ini): `compose = 1`, `hud_scale_mode = 0`, `hud_corners = 1`,
`view_width = view_height = 0`, `vsync = 1`. `--faithful` sets `compose = 0`.

## Deviations / gaps

- Translucent HUD parts (blended panels, the radar's surround) are blended with the *downsampled* view and
  shown as HUD-resolution blocks over the hi-res view - the briefing's design. A future GPU renderer could
  blend in RGB at display resolution.
- The second 2D pass doubles the 2D drawing (cheap, see numbers). Alt+X (debug screenshot) inside
  render_frame_draw would fire twice per frame in composed mode.
- Motion blur / 2x2 smoothing in the composed view are task A's (`render_view_ext`). The faithful
  `render_view`'s slope low-pass state is not advanced while composing (A's ext keeps its own) - switching
  modes mid-flight gives one slightly different slope offset.
- The HUD corner blocks are fixed 640-space rectangles of the flight HUD as the original draws it (they
  are the same in 320x200, halved); a HUD that drew elsewhere would simply stay centred (crossing check).
- Note for task A: `Camera.pitch` is a horizon offset in 1/256 of the view *width* (render.h). For a
  wide view the same pitch must move the horizon by the same fraction of the *height* as the original does
  (i.e. scale by `height * 4/3` instead of `width`), otherwise looking up / down is exaggerated on 21:9.

## Requested shared-file changes

None required. (`settings.h` section B edited in place as allowed.)

## mcport integration (exact code for `src/mcport/main.cpp`; task D owns it)

Written against the round-6 main.cpp; line references are to that version.

1. Includes (after `#include "net_tcp.h"`):
```cpp
#include "compose.h"
#include "settings.h"
```

2. Two helpers after `frame()` (around line 106):
```cpp
// Round 7 (compose.h): present the frame - composed (native view + HUD layer) or the original way.
static void present_frame() {
    if (compose_installed()) {
        int dw, dh;
        s_plat->output_size(&dw, &dh);
        s_plat->present_composed(compose_frame_for_present(dw, dh, frame()));
    } else {
        s_plat->present();
    }
}
// The game's 2D pass over the view: composed, it runs inside compose_draw_frame (twice when a view was
// placed - it must not write game state; render_frame_draw / overlay / pointer do not).
template <class F> static void draw_view_frame(F &&draw) {
    if (compose_installed()) {
        int dw, dh;
        s_plat->output_size(&dw, &dh);
        compose_draw_frame(dw, dh, frame(), draw);
    } else {
        draw();
    }
}
```

3. After `s_plat = &plat;` (line 303), once the settings are loaded (task E's `config_load` runs before):
```cpp
    if (g_settings.compose == 1) compose_install();
    // plat.set_vsync(platform_options.vsync); if (platform_options.fullscreen) plat.set_fullscreen(true);   (task E)
```

4. MC_SHOT in composed mode: in `maybe_screenshot`, replace the body of `if (n == frame_no) { ... }` by
```cpp
        if (n == frame_no) {
            char name[64];
            std::snprintf(name, sizeof name, "mcport_shot_%ld.ppm", n);
            int w = s_plat->fb_w, h = s_plat->fb_h;
            std::vector<uint8_t> rgb;
            if (compose_installed()) {                 // the composed image at the drawable size
                const ComposeOutput &o = compose_output();
                w = o.display_w; h = o.display_h;
                rgb.resize((size_t)w * h * 3);
                compose_to_rgb(o, g_display_palette6, rgb.data());
            } else {
                rgb.resize((size_t)w * h * 3);
                for (int i = 0; i < w * h; i++) {
                    const uint8_t *c = g_display_palette6 + s_plat->fb[i] * 3;
                    for (int k = 0; k < 3; k++) rgb[(size_t)i * 3 + k] = (uint8_t)(c[k] << 2 | c[k] >> 4);
                }
            }
            if (SDL_RWops *f = SDL_RWFromFile(name, "wb")) {
                char hdr[32];
                const int hn = std::snprintf(hdr, sizeof hdr, "P6\n%d %d\n255\n", w, h);
                SDL_RWwrite(f, hdr, 1, (size_t)hn);
                SDL_RWwrite(f, rgb.data(), 1, rgb.size());
                SDL_RWclose(f);
            }
        }
```
   and move the `maybe_screenshot(frame_no++)` call after `present_frame()` (the composed output of this
   frame is final once presented; `compose_frame_for_present` may build it there for 2D-only frames).

5. Every `plat.present();` in the loop (the palette-fade path, line 473, and the end of the frame, line 569)
   becomes `present_frame();`.

6. The level / demo drawing (lines 554-559):
```cpp
                draw_view_frame([&] {
                    render_frame_draw(frame(), g_state->local_player);
                    ui_draw_debug_overlay();
                    if (run == RUN_LEVEL) mouse_cursor_draw(frame());
                });
```
   and the free-camera viewer (lines 561-563) through the seam so it is composed as well:
```cpp
                const FrameBuffer fb = frame();
                draw_view_frame([&] {
                    render_set_view_window(fb, g_state->view_size);   // free camera: the view only
                    render_view_frame(fb, cam);
                });
```

7. Nothing else: mouse positions / relative motion arrive in game-frame pixels in both presentations
   (platform), `warp_pointer` keeps calling `warp_mouse(fb_x, fb_y)`, Alt+Enter is handled in `poll`, the
   front end and the full-screen 2D screens need no wrapping (`compose_frame_for_present` shows any frame
   drawn outside `draw_view_frame` as a plain 4:3 frame), `video_mode_changed` / `plat.resize` keep working
   (the next composed frame uses the new frame size).

## Next round

- With the real `render_view_ext`, re-measure the whole frame at 1080p / 1440p / 4K (view render
  dominates; present is a small fraction).
- HUD scaling options for 320x200 in integer mode (the 1.28 aspect at 1080p; offer "integer, square
  pixels" vs "fit").
- A GPU renderer (round 8?) would take the same ComposeOutput: the palette lookup and HUD blend move into a
  shader (upload 8-bit indices + a 256-entry palette texture: a quarter of today's upload).
- Config (task E): `compose`, `view_width/height`, `hud_scale_mode`, `hud_corners`, `fullscreen`, `vsync`.
