# Port round 8 (Phase 3 round 2) - 2026-10-07

Single-session round (no parallel agents). Focus set by the user after the round-7 play-test.
All 47 ctest tests pass; every reference (movie 0 per tick and pixels, 8 levels, generation of all levels,
the original replaying port recordings) is unchanged.

## 1. Segmented creatures flickering for one frame (render-only fix)

Measured with an awake dragon on level 0 (local player held 8 cells from the head; per-tick sim distance
segment -> parent, `timer_a`):

- `creature_proximity_wake_timer_46960` counts the head's `timer_a` 16 -> 0 and copies it to the segments.
  On the tick it reaches 0, the head does not re-arm (that happens on the next tick: head 0x10, segments 0x12),
  so for exactly one tick in 17 every segment has `timer_a == 0`.
- `creature_segment_update_18050` treats `timer_a == 0` as asleep: segments with `(tick & 3) == 0` (a quarter of
  them) snap onto their parent's position; the next segment is then ~200-260 units from its parent. The next
  tick the awake rule (`speed` units behind the parent, towards the segment's old position) restores the chain.
  This is the original's game state; the original renderer shows the same one-tick kink (inside its fog).
- The extended renderer laid sleeping segments out itself (round-7 follow-up) and switched that layout on for
  exactly that tick - from a stale cache, i.e. straight behind the parent's heading - and with interpolation the
  snapped segments were lerped from the snap position on the following tick.

Fix (`render_ext.cpp thing_pos`): every body segment (class 5, state 0x78) is drawn follow-the-leader, awake or
asleep: `speed` units from its drawn parent, in the direction of where it was drawn last frame - the rule the
awake simulation applies once per tick, applied per frame. A chain that is new to the view starts from the
interpolated simulation position when it lies 0.5..2 x `speed` from the drawn parent, else straight behind the
parent's heading. The simulation is untouched; the faithful renderer is untouched.

`render_ext_segments_test` part 3: awake creature, 120 ticks, 3 interpolated frames per tick, 7 timer resets with
28 snaps; every frame each segment must be `speed` +-2 from its drawn parent and move per frame no further than
twice the head's step + 8. The round-7 code fails it (spacing error 170.59, jump 70.49); the new code: 0.73 / 0.

## 2. Possession range (`game.possession_range_pct`)

What limits the range (class 9 type 1, `projectile_homing_update_448b0`):

- lifetime: `projectile_create_type1_37d30` sets `max_health = 0x1000 / 0x180 = 10`; the update ends the shot
  when `--health < 0`, so it moves 11 times (`speed_cur` = 0x180 + the caster's speed);
- target: only on the first tick (`flags & 2`), `projectile_pick_target_45f00` case 1 picks among mana balls
  (and wizards) inside a 0x71 cone and within 0x1400 (`projectile_target_score_46470`); after that it homes.
- `aux = 0xc8` is clamped to 0x10 by the pick and read nowhere else for this type; `home` (0x2800 ahead) is not
  read by the homing update. Neither limits the range.

Setting: `PortSettings::possession_range_pct` (100..200, faithful 100, mcport play default 130). The lifetime
becomes `round(11 * pct / 100)` moves (130 -> 14, +27 %; whole ticks only), the pick radius `0x1400 * pct / 100`
(130 -> 26 cells). At 100 both are the original's values exactly (`range` 0x1000 is passed unchanged).

Gameplay rules vs. overrides (`settings.h`): game logic reads `gameplay_rules()`, never `g_settings` directly.
`gameplay_force_rules` overrides the setting for a movie being played (the recording's rules; `mvi` movies =
faithful) and for a network session (the host's). Recording: non-faithful rules -> `mvx` / `gax` / `max` files,
`DemoExtHeader` version 2 (thing_slots may then be 1000) followed by `DemoExtRules`; faithful rules with an
extended pool still write version 1.

Tests: `possession_range_test` (straight shot: 10 / 13 ticks seen alive at 100 / 130 %, ~0x180 per tick; a
mana ball 24 cells ahead picked only at 130 %; a forced 100 wins over a 130 setting); config_test `rules_movie`
(record at 150 %, play back with the setting at 100: 300 ticks identical; the header; movie 0 plays faithful).
Not asserted: that an AI wizard cast Possession during those 300 ticks.

## 3. Round-7 leftovers done

- Network agreement (`net.h NetGameRules`, `net_agree_rules` / `net_apply_rules` / `net_release_rules`, replaces
  `net_agree_u32`). Round 7's attempt broke net_test because only the front-end lobby did the extra exchange -
  net_test's scripted peers (part 3 processes, part 4's other player) did not, so the block exchanges paired
  up wrongly. Now every peer, scripted or not, does exactly one rules exchange right after the level choice.
  net_test: in the TCP lockstep parts player 0 (host) brings 3000 slots / 150 %, the others 1000 / 100; all adopt
  the host's and stay identical (2000 / 3201 per-tick checksums); part 4 checks the lobby adopted 4000 / 140 and
  `net_shutdown` released it. Release also happens in `game_after_frontend` when a level starts outside a
  network game.
- Interpolation slot identity: `thing_slot_generation(idx)` (thing.h; ++ on every `thing_alloc` of the slot,
  outside all state); `PaceSlot.gen` must match for a slot to lerp.

## 3b. Flight keys (user request)

`PortSettings::keys_wasd` / `keys_book_tab` (config `[keys] wasd`, `book_tab`; faithful off, play default on).
Done in `input.cpp local_input_flight` (not as a platform key remap, which would have turned Alt+S into Alt+Down
and typed arrows into the chat line): W / S / A / D are read next to Up / Down / Left / Right; Tab is accepted
where Enter opens the book (flight) and closes it (book, mode 1). The original reads no plain W / A / S / D or
Tab in those modes, so nothing is displaced. The commands sent are the original's (movies / network unaffected).
input_test `test_flight_keys`: default ignores them; on: bits 1 / 2 / 4 / 8, or-ed with arrows, Alt+S still
cmd 0xa without movement, Tab opens (cmd 0x14 arg 2) and closes (arg 0) the book.

## 4. Build

`/INCREMENTAL:NO` for MSVC (src/CMakeLists.txt). Twice this round an incrementally relinked test (game_test,
effects_test) crashed with an access violation before `main` (no output, the crash handler not yet installed);
deleting the `.ilk` and linking the same objects again gave a working executable.

## Open

Controller on real hardware + analog speed; in-game options / save-slot menu; the faithful renderer's slope
low-pass at high frame rates; GPU path for filtered textures / HD packs; savestates do not carry the gameplay
rules (a load continues with the current settings).
