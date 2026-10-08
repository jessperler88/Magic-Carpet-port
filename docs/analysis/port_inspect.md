# Port round 10, task C: projection export, display pointer, Thing inspector, overlays (2026-10-08)

Phase 4 round 1 (`docs/port/BRIEFING_round10.md` task C). Everything here is port-only and **render-only**:
nothing writes game state, nothing is read by the simulation, nothing is recorded / replayed / hashed. With
nothing switched on, no code of this task runs in a frame (the faithful renderer gets one extra
`if (s_anchor_stage)` per drawn Thing), and every pixel reference is identical.

## 1. What was built

| file | what |
|---|---|
| `mcengine/render.h` (new section at the end) | projection export API: `RenderViewInfo`, `render_project_world`, view / frame / HUD-640 mappings, `render_pick_ground`, `RenderAnchor` list + `render_pick_thing`, capture bracket |
| `mcengine/render_things.cpp` | the implementation (renderer-agnostic part; it is linked wherever a renderer is); faithful anchors pushed in `render_thing` right before `render_sprite_scaled(1)` while a capture is open |
| `mcengine/render_ext.cpp` | the extended renderer records its projection (`F`) into `RenderViewInfo` every frame and pushes an anchor per drawn Thing (not reflections / shadows) while a capture with anchors is open |
| `mcengine/compose.h/.cpp` | `compose_view` records the view-buffer -> game-frame mapping; `compose_display_to_view` / `compose_view_to_display` (display pointer <-> hi-res view pixel) |
| `mcengine/debug_overlay.h/.cpp` (new) | overlay registry + 6 built-in overlays, Thing inspector panel / text, follow camera, pointer hover, per-frame bracket |
| `mcport/platform.h`, `platform_sdl.cpp` | `Input.display_x / display_y` (pointer in drawable pixels, both presentations), `Input.mouse_m`, `Platform::composed()`, `relative_mouse()`, `refresh_pointer()`, `display_to_frame()` |
| `mcport/inspect_tool.h/.cpp` (new) | cursor mode (absolute pointer, steering frozen), mouse picking, tool keys, follow camera for main.cpp |
| `tests/project_test.*`, `tests/overlay_test.*` (new) | see section 2 |

No `settings.h` field and no ini key were added (section 6).

### 1.1 Projection export (render.h)

- **View space** = the pixels of the target the 3D view was drawn into: the faithful renderer's render target
  (the view window inside the game frame), `render_view_ext_target`'s window, or the compositor's hi-res view
  buffer. Every drawn view leaves a `RenderViewInfo` (`render_view_info()`): renderer kind, serial, view size,
  camera after the slope low-pass, the faithful `g_rcam` copy or the extended renderer's double projection,
  and the view -> game-frame mapping (`frame_x0 + v * frame_sx`; identity + window offset, or the compositor's
  scale through the HUD rectangle).
- `render_project_world(x, y, z, &sx, &sy, &depth)`: the faithful path is `render_thing` + `thing_project`
  verbatim (integer, the original's rounding, 16-bit torus differences); the extended path is
  `emit_cell_things` + `project()` (same double expressions, so bit-identical). False when depth <= 0x40.
- `render_view_to_frame` / `render_frame_to_view`, `render_frame_to_hud640` (640-wide virtual space of
  ui_draw.h: x * 640 / w, y * 480|400 / h), `render_project_world_hud640`.
- `render_pick_ground(sx, sy, &cx, &cy[, &wx, &wy, &wz])`: un-rolls the pixel, marches the camera ray
  (step 8 + t/64 world units, up to 1.5 x the draw radius) against `terrain_sample_height`, bisects the
  crossing (24 steps). Same code for both renderers.
- **Anchors**: `RenderAnchor {slot, generation, sx, sy (bottom centre), w, h, depth, x, y, z (drawn position,
  interpolated / segment layout in the extended renderer)}`; `render_anchors(&n)`; `render_pick_thing(sx, sy,
  max_dist)` = the front-most anchor whose box contains the point, else the nearest box within max_dist.
- **Capture bracket** `render_capture_begin(fb, anchors)` / `render_capture_end()`: installs a pass-through
  wrapper as `g_render_view_override` (it calls the override that was there - the compositor, the extended
  target renderer - or `render_view`; the pixels are the same) and restores it at the end. How the wrapper
  knows the faithful renderer ran: `render_landscape_29050` writes `g_rcam.fog_far2 = kFogFar2` every frame
  before anything reads it, so the wrapper zeroes it before the call and puts it back if nothing rewrote it
  (the extended renderer ran, or nothing did - the compositor's second pass). The faithful anchors are staged
  and committed only when the faithful renderer ran, so the compositor's second pass keeps the first pass's.

### 1.2 Display pointer

- `Input.display_x / display_y`: the pointer in drawable pixels, from the motion events (composed: full
  display precision; the original presentation: the logical pixel's centre mapped back - the view there has
  game-frame resolution anyway); `Platform::refresh_pointer` reads the OS pointer once when a cursor mode
  starts. `Input.mouse_x/y` are unchanged (game-frame pixels).
- Pointer -> view space (`inspect_tool.cpp pointer_view`): composed = `compose_display_to_view(compose_output(),
  display_x, display_y)` (the hi-res buffer, so a 4K view picks at 4K precision); otherwise
  `Platform::display_to_frame` (logical scaling) then `render_frame_to_view`.
- **Cursor mode** (Home or the middle mouse button): SDL relative mode off (the OS pointer shows), the game's
  pointer is held at the centre (= no steering: steering is the pointer's offset from the centre) and the
  mouse buttons are not fed to the game (no spells). The flight keys (arrows, 1..0, ...) still work. Left
  click = inspect the Thing under the pointer (and pick the cell), right click = pick the cell only. The mode
  is held while the pause menu is open and ends with the level. In the spell book / map screen the game's own
  pointer is frozen at the centre too while the mode is on (turn it off to use the book).

### 1.3 Thing inspector (debug_overlay.cpp)

Panel (640-space, font 1, dark `ui_shade_rect` 0x34, top right below the status bars; on the map screen below
its view window) with: slot / generation / FREE / "(slot reused)" / [follow]; class + name, type + model name;
state + the Table A handler name (gen/dispatch_tables.h); flags / prop_flags / sprite / frame / draw type;
position, cell, extents; yaw / pitch / speeds; health / max, mana / total / cost; owner, parent, child, target
(+ signature), killer, attacker, caster, mana owner; timers, aux, duration, cast ticks, z velocity; home;
pending damage slots; the MoveDesc (turn, height band, sight, fov, think period); the cell list it is on (and
whether it is really on it). For a Thing with a player block: player number, human / computer, name; for
class 3 the `PlayerBlock`: **AI mode with its name**, castle + level, mana, transit, regen, invulnerability,
kills, aggression / accuracy / reaction, target speed, hand slots, tether. The picked cell adds a line with
type / height / light / flags and its Thing list. In 320x200 a compact subset is shown (the HUD font has the
same size relative to 640-space, so the full panel would cover the view). `debug_inspect_lines(slot, &out)`
gives the same text to the console. The follow camera (`debug_inspect_follow_camera`): 3 cells behind the
Thing along its yaw, 1.5 above (at least 1 cell over the ground), pitch -0x10, interpolated with
`g_render_interp` like the extended renderer's Things.

### 1.4 Overlays (registry: `debug_overlay_register(name, help, fn, needs_view)`)

| name | what |
|---|---|
| `grid` | 21 x 21 cells around the camera: projected cell corners (height byte * 0x20) joined by lines, clipped to the view; the camera's cell orange, the picked cell red |
| `labels` | `slot class.type sState [aiN]` above the 48 nearest drawn Things (16 in 320x200), the inspected one red |
| `anchors` | the anchor boxes picking uses |
| `occupancy` | `g_cell_things` chain length per cell as dots on the radar (flight radar inside its circle, the map screen's whole-map radar 2x2): 1 white, 2 green, 3-4 yellow, 5-8 red, 9+ magenta, legend on the map screen; the radar maths of hud.cpp inverted |
| `damage` | health losses of players, creatures and damageable Things (prop_flags & 3) as rising numbers for 40 ticks; from a render-side health history sampled once per tick (no hook exists, see 4) |
| `net` | network / local game, `net_sync_stats()` (exchanges, checks, mismatches, first mismatch, last local checksum), `net_state_checksum()` + tick, the mode block (mode, seed, mode tick), live Things / pool size; marked place for task E's `NetChecksumParts` |

`overlay all on` / `overlay none` switch every overlay. The hovered Thing (cursor mode) gets a white box, the
inspected one a red box, whether or not an overlay is on. All lines are drawn into the game frame (frame
pixels, clipped to the view rectangle), all text with `ui_draw_text` in 640-space - in the composed mode the
compositor puts them into the HUD layer like the HUD.

## 2. Verification

- **project_test** (mc_unit_test, level 38 after 50 ticks):
  - faithful 320x200 / 640x480, plain camera, roll 0x60 + pitch, reduced view window 0x1c: all 24 Thing blits
    the sprite probe `g_sprite_blit_probe` sees are exactly at `render_project_world`'s point and equal the
    anchor list (slot, x, y); the frame drawn inside a capture is byte-identical to one drawn without; the
    override is restored; the view -> frame mapping is the window offset.
  - extended 320x200 / 640x480 / 1920x1080 (+ rolled 1920x1080), draw distance 48: every anchor (27..32)
    re-projects exactly onto itself and sits at its Thing's position.
  - ground pick round trip (cell centre projected, picked back): within 8 cells **100 %** for every case
    (43..72 visible cells per case, none hidden, wrong or missed); information: up to 18 cells faithful
    257/257 .. 281/282; up to 40 cells extended: about half hidden behind nearer terrain (level 38's valley),
    1-5 % wrong (a pixel spans more than a cell at grazing angles far away), a few misses (the ray passes over
    a crest).
  - `render_pick_thing` at each anchor's box centre returns an anchor containing the point that is not behind it.
  - composed 1920x1080 over 640x480 (compositor installed, both passes): view -> frame -> display equals the
    compositor's own view -> display for all 31 anchors, `compose_display_to_view` inverts it for the 23 on
    screen, ground picks 72/72.
- **overlay_test** (level 38 after 300 ticks, every overlay on, a Thing inspected, a cell picked; PPMs looked
  at): flight 640x480 (116946 pixels changed by the overlays), 320x200, map screen (occupancy 2826 pixels),
  extended renderer at 640x480, composed 1920x1080; **GameState, Config, the four maps, the cell heads and
  `net_state_checksum()` byte-identical before and after every drawn frame**; a 37-point health loss between
  two ticks shows as "-37"; registry (built-ins, unknown name refused, toggle, `all` / `none`, an extra
  overlay registered and called); every computer wizard's panel has its "ai mode" line; follow camera behind
  and above the Thing; with everything off the capture is closed and the override restored.
- Gates run in `build_C` (Release): render_reference_test, render_reference_hud, render_reference2_test,
  render_reference_options, render_reference_fe_test (33/33 pixel-identical), render_ext_test,
  render_ext_segments_test, render_ext_mana_test, compose_test, hud_test, sprites_test, engine_test,
  reference_test, reference_player_test, game_menu_test: all pass. Zero warnings (Debug and Release) for
  project_test, overlay_test, mcport.
- **mcport end to end** (a scratch copy of src with the main.cpp code of section 5, Release, headless
  `SDL_VIDEODRIVER=dummy`, `play 38`, `MC_TEST_INPUT` key presses / clicks, `MC_SHOT`): keypad 1/2/3/5 turn on
  grid / labels / anchors / damage, Home turns the cursor mode on, a click on a mana ball 1280x960 composed
  inspected slot 46 (view 590,168 -> anchor 46 at 590,176 24x20), keypad 8 followed it (screenshot: the
  camera behind the falling ball, panel "[FOLLOW]"); with `--set display.compose=0` (320x200 scaled) a click
  inspected a townie. Screenshots in the scratch dir were checked by eye.

## 3. Deviations / gaps

- **Composed widescreen**: overlays are drawn into the 4:3 game frame, so nothing appears in the side areas of
  a 16:9 display, and labels inside the top HUD blocks move with them when `hud_corners` anchors those blocks.
  The real fix is the native-resolution overlay layer (phase4_code_platform.md section 8), not this round.
- **320x200**: the HUD font keeps its size relative to the 640-space, so text covers a lot; the panel is
  compacted and labels limited to 16, but 640x480 (R) or the composed mode is the better place to inspect.
- **Damage numbers** come from a render-side health history (sampled when the local tick counter advances;
  regeneration and effect lifetimes are excluded by class / prop_flags), not from the damage code: several
  hits in one tick show as one number, a hit and a heal in the same tick cancel. See 4 for the hook.
- Ground picks far away are approximate (a pixel covers several cells near the horizon); the ray ignores the
  water wave offset.
- The extended renderer's anchors use the interpolated / sleeping-segment position it drew, the faithful
  ones the Thing's position (that renderer does not interpolate) - `RenderAnchor.x/y/z` says which.
- The cursor mode freezes mouse steering and the book's pointer; the keyboard still flies.

## 4. Requested shared-file changes

None are required to build or pass: CMake globs the new files, all hooks are in my files.

**Proposal for round 13 (damage numbers / "damage slot writes" overlay)** - an observer hook, render-only
consumers, never a state write:
```cpp
// spatial.h
extern void (*g_hook_damage_applied)(const Thing *victim, uint16_t attacker, int32_t amount, int slot);
```
called right after each `health -= damage_slots[k].amount` site: `castle.cpp:61`, `creature_common.cpp:108`
and `:153`, `level_features.cpp:539`, `player.cpp:666` and `:695`, `scenery.cpp:28`; and a dealer-side
`g_hook_damage_queued(attacker, victim, slot, amount)` at the end of `thing_add_pending_damage`
(`spatial.cpp:199`). Null by default (one predictable branch per hit).

**Task E**: when `NetChecksumParts` lands, the `net` overlay should print the parts that differ / the first
mismatching part - one `L.push_back(...)` at the marked line in `debug_overlay.cpp draw_net`.

## 5. main.cpp integration (task D; exact code, tested in a copy of today's main.cpp)

1. Includes (after `#include "debug_cmd.h"`):
```cpp
#include "debug_overlay.h"
#include "inspect_tool.h"
```
2. Key loop (the `for (int i = 0; i < in.key_event_count; i++)` loop), right after
   `if (!in.key_events[i].down) continue;` and before `debug_key_action`:
```cpp
                // round 10 (task C): inspector / overlay / cursor-mode keys (Home, keypad; not game keys)
                if (inspect_tool_key(plat, in.key_events[i].scancode, kmods, run == RUN_LEVEL)) {
                    in.key_events[i].scancode = 0;
                    continue;
                }
```
3. Right before `if (devices && !s_menu.is_open() && !dcam_input) {`:
```cpp
        // round 10 (task C): the cursor mode (absolute pointer, picking) owns the mouse while it is on; held
        // while the pause menu is open, switched off outside a level
        const bool cursor_mouse = s_menu.is_open() ? inspect_tool_cursor_mode()
                                                    : inspect_tool_mouse(plat, in, run == RUN_LEVEL);
```
4. In that block, replace `if (level_mouse) {` (the relative-motion steering) by:
```cpp
            if (level_mouse && cursor_mouse) {
                // round 10 (task C): cursor mode - the game's pointer stays at the centre (no steering)
                s_level_mx = 320; s_level_my = virtual_h() / 2;
                mouse_acc_x = mouse_acc_y = 0;
                input_mouse_move(s_level_mx, s_level_my);
            } else if (level_mouse) {
```
   and the button line by:
```cpp
            // (cursor mode: the mouse buttons pick, the game does not see them)
            const bool ml = (in.mouse_l && !cursor_mouse) || pad.mouse_l, mr = (in.mouse_r && !cursor_mouse) || pad.mouse_r;
```
5. The game-view frame: after D's `g_debug_camera.cam = ...` lines, around `draw_view_frame`:
```cpp
                // round 10 (task C): the inspector's follow mode drives the debug camera
                Camera follow_cam;
                if (run == RUN_LEVEL && inspect_tool_follow_camera(&follow_cam)) {
                    g_debug_camera.active = true;
                    g_debug_camera.cam = follow_cam;
                }
                debug_overlay_frame_begin(frame());           // round 10 (task C): projection capture when needed
                draw_view_frame([&] {
                    render_frame_draw(frame(), g_state->local_player);
                    ui_draw_debug_overlay();
                    debug_overlay_draw(frame(), g_state->local_player);   // round 10 (task C): overlays, inspector
                    ... (unchanged: draw_pace_overlay, menu / pointer)
                });
                debug_overlay_frame_end();
```
   (Alternative for the follow mode: D's `dcam_follow(debug_inspect_slot())` when `debug_inspect_follow()`
   turns on - that stops feeding the game, the block above does not.)
6. Optional, after `game_level_begin(...)` succeeds and after a state load: `debug_overlay_reset();` (forgets
   the inspected slot and the damage history; without it the panel shows "(slot reused)").
7. Console host commands (task B's `HostCmd`, where main.cpp executes `take_host_commands()`):
```cpp
            case HostOp::OVERLAY:
                if (c.sub.empty() || c.sub == "list") mclog(MCLOG_INFO, "%s", debug_overlay_list().c_str());
                else if (!(c.on < 0 ? debug_overlay_toggle(c.sub.c_str()) : debug_overlay_set(c.sub.c_str(), c.on != 0)))
                    mclog(MCLOG_WARN, "overlay: unknown '%s' (overlay list)", c.sub.c_str());
                break;
            case HostOp::INSPECT:
                if (c.sub == "off") { debug_inspect_set(-1); debug_inspect_set_cell(-1, -1); }
                else if (c.sub == "cursor") inspect_tool_set_cursor_mode(plat, true);
                else {
                    debug_inspect_set(c.n);
                    std::vector<std::string> lines;
                    debug_inspect_lines(c.n, &lines);
                    for (const std::string &l : lines) mclog(MCLOG_INFO, "%s", l.c_str());
                }
                break;
```
   and in `cam follow <slot>`: also `debug_inspect_set(slot);` so the panel shows what the camera follows.
   A cursor spawn / teleport (B's builders) can take the picked cell from `inspect_tool_cursor_cell(&cx, &cy)`.

## 6. Keys, overlay names, settings (user documentation)

Keys (fixed this round in `inspect_tool.cpp`; none is read by the game, task D owns Pause / End / PageUp /
PageDown / Insert / Delete):

| key | action |
|---|---|
| Home, middle mouse button | cursor mode on / off (pointer visible, mouse steering and mouse spells off) |
| left click (cursor mode) | inspect the Thing under the pointer; also picks the ground cell |
| right click (cursor mode) | pick the ground cell (panel line + red outline) |
| Shift+Home, keypad . | inspector off |
| Ctrl+Home, keypad 8 | follow the inspected Thing with the camera on / off |
| keypad 7 / keypad 9 | inspect the previous / next live Thing slot |
| keypad 1 .. 6 | overlay grid / labels / anchors / occupancy / damage / net on / off |
| keypad 0 | all overlays off |

Console (task B's grammar): `overlay` (list), `overlay <grid|labels|anchors|occupancy|damage|net|all|none>
on|off`, `inspect <slot>`, `inspect cursor`, `inspect off`, `cam follow <slot>`.

Settings / ini keys: none added. Proposed for the integrator if the keys should be configurable: `[keys]
cursor_mode = Home`, `inspect_off = Shift+Home`, `inspect_follow = Ctrl+Home` (KeyChord like the other
`[keys]`), read by `inspect_tool_key` instead of the constants.

## 7. Next round

- Round 11 (32-bit world): `render_project_world` / `render_pick_ground` take the int32 positions and the
  world descriptor's wrap; the grid / occupancy loops go through the world accessors instead of 256 / `mc_cell`.
- Round 13: the native-resolution overlay layer (`ComposeOutput` third layer) so labels / plates / numbers are
  sharp and cover widescreen sides; `g_hook_damage_applied` (section 4) for exact damage numbers.
- Round 15 / 16 (units, RTS camera): drag-box selection = anchors inside a rectangle (`render_anchors`), orders
  at `render_pick_ground` cells through packets; the cursor mode becomes the RTS pointer.
