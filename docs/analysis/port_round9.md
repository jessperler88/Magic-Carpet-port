# Port round 9 (Phase 3 round 3), 2026-10-07

One session, no agents. 49/49 ctest, every reference (movie 0, levels, player recordings, pixel gates)
still identical: nothing below changes the simulation with the default `PortSettings`.

## 1. Play-test bug: a dead wizard's mana stays in their colour far away

Report: after killing a computer wizard and claiming their body, all the mana that wizard had claimed
belonged to the player, but the far-away balls still showed the wizard's colour until the player came close.

**The transfer is the original's rule**, not a port bug:
- `player.cpp` (the death of a player Thing, player_type3 handler): a class 10 type 0x28 *mana hoard
  marker* is created at the body and every mana ball whose `mana_owner` was the wizard is re-owned by it.
- `effect_mana_hoard_update_25f10`: when a player claims the hoard (Possession, damage slot 1) while nobody
  owns it, every Thing owned by the hoard passes to that player and the hoard is deleted.

**The colour was a port (far-view) bug.** A ball's sprite (owner colour + size by mana) is chosen by
`mana_ball_update_sprite_25e20`, which `effect_mana_ball_update_25980` only calls while the ball is awake
(`timer_a != 0`, within 24 cells of the local player). The original never draws a ball that far (its view
ends at ~20 cells), so a stale sprite never showed; the extended renderer (draw distance 125) drew it.
While hoard-owned the balls should look unowned (base sprite 0x34: the hoard is class 10, not a player).

Fix (render-only): `mana_ball_sprite(const Thing *, int *level)` (constructors.h) is the pure part of
`mana_ball_update_sprite`; `render_ext.cpp emit_cell_things` draws a class 10 type 0x27 Thing with the
sprite it computes instead of `Thing.sprite`. The faithful renderer is unchanged (pixel references).
Test `render_ext_mana_test`: a sleeping ball 64 cells away, owned by a wizard, then by a hoard, then
claimed by the local player through the hoard; every frame must equal the frame drawn after forcing the
sprite refresh; fails on the old code (32657 / 53918 pixels differ), and rendering leaves the GameState alone.

## 2. Save states carry the gameplay rules

`savestate_save_file` writes a `RULE` chunk (u32 version 1, u32 `possession_range_pct` from
`gameplay_rules()`); `savestate_load_file` forces those rules (`gameplay_force_rules`) for the rest of the
level - `game_after_frontend` -> `net_release_rules` drops them at the next level. A state without the chunk
(rounds 7 / 8) keeps the current settings, as before. `MC_PORT_VERSION` 0x00030003. config_test
`rules_state`: saved with 150 %, loaded with the setting at 100 -> rules forced to 150, 300 ticks identical.

## 3. In-level pause menu (`mcport/game_menu.*`)

The original's Esc in flight leaves the level at once (commands 0x1b / 0x1d). In mcport a level now opens a
pause menu on `keys.menu` (default Esc; `none` = the original's Esc). Pages:
- **Resume**, **Save state** (slots 1..9 and the quick slot 0 with level / tick / date; a used slot asks
  "Enter again to overwrite"), **Load state** (used slots only), **Options**, **Leave level** (hands the
  game the original's Esc), **Quit Magic Carpet**.
- **Options** edit mcport.ini keys through `config_get` / `config_apply`, applied at once: renderer, draw
  distance, fog start, far detail, fog mode, HUD scaling / corners, radar zoom, fullscreen, smooth motion,
  frame-rate cap, Possession range (shows the level's forced value; disabled in a network game), Thing pool
  (next level), WASD flight, Tab spell book. The changed keys are written into mcport.ini when the menu
  closes with the new `config_set_keys` (in place: replaces the key's `key = v` / `# key = v` line, keeps
  comments and unknown keys, appends missing keys, creates a missing file).
- The level is held while the menu is open (no ticks, interpolation held), except in a network game, where
  it keeps running and save / load are disabled. Opening releases every key / button the game held.
- Input: keyboard (arrows, Enter / Space, Esc / Backspace = back, Page Up / Down, Home / End), mouse
  (pointer = the book's pointer sprite, hover selects, left click activates - on an option's value the left
  quarter steps down, the right quarter up - right button = back, wheel scrolls), controller (menu context:
  stick / d-pad move the pointer, A click, B back; Back opens the menu as before it sent Esc).
- Drawn with the game's HUD font into the 640-wide virtual screen after the 2D pass, so it composes with
  the HUD layer at native resolution. Shade rows (`ui_shade_rect`): 0x20 = unchanged, above darker
  (0x2c ~ 65 %, 0x34 ~ 35 %), below fades to the fog colour.
- `Input.wheel` added to the platform layer.

Test `game_menu_test`: config_set_keys (create / in place / append / read back), navigation incl. disabled
rows, options stepping / clamping / wrapping / values between choices, changed keys, fullscreen command,
save / load pages with overwrite confirmation, pointer hover / click / back, drawing over a rendered level
(panel darker than the scene, screenshots game_menu_main.ppm / game_menu_options.ppm). Checked in the real
game headless (MC_TEST_INPUT): ticks stop while open and resume, an options change lands in mcport.ini,
Leave level reaches the front end.

## Open (round 10 candidates)
- Controller on real hardware + analog speed; key rebinding inside the menu; audio volumes in the menu
  (the mixer reads its gains at start-up).
- Faithful renderer's per-frame slope low-pass at high frame rates; GPU path for filtered textures / HD packs.
- More play-test feedback.
