# The 1995 engine for Hidden Worlds (`engine1995()`)

HIDDEN.EXE is a 1995 build. Besides the Hidden Worlds changes (`hidden_worlds_re.md`, `world_hidden()`)
it carries the 1995 versions of the functions that the 1996 carpet.exe (the port's reference) changed.
CD CARPET.EXE (1995 base game) and HIDDEN.EXE are identical in all of them (Appendix A of
`hidden_worlds_re.md`), so the CD exe is used for the addresses below; HIDDEN has the same code at
shifted addresses.

The port now implements every simulation-relevant 1995 difference behind one predicate
(`src/mcengine/world_set.h`):

```c++
extern bool g_engine1995_force;                 // MC_ENGINE1995=1 at world_set_init (default off)
inline bool engine1995() { return g_engine1995_force || world_set() == 1; }
```

It is true while a Hidden Worlds level is active. With `MC_ENGINE1995=1` it is also on for base levels,
so the CD CARPET.EXE recordings (`cd95_*`) can serve as references for the same code. Without the
variable, base levels run the 1996 code unchanged.

## Results

| reference | what | result |
|---|---|---|
| `hw_level00` | HIDDEN.EXE, DDLEVELS 0, idle player, 5000 ticks (1399 dumps) | **0 divergences** (before: first divergent tick 3, 262 divergent ticks) |
| `hw_level12` | DDLEVELS 12, 8 players | **0 divergences** (before: tick 3, 928) |
| `hw_level05`, `hw_level09`, `hw_level18`, `hw_level24` | new recordings made for this round (`run_level_hw.py 5 9 18 24`) | **0 divergences** each |
| `cd95_level12` with `MC_ENGINE1995=1` | 1995 base exe, base level 12 | **0 divergences** (without the variable: tick 142, as before) |
| `cd95_level00` (both settings) | 1995 base exe, base level 0 | 0 divergences |
| `hw_gen` (25 generations) | | 0 of 25 diverge (also with `MC_ENGINE1995=1`) |
| `cd95_gen` (3 generations) | | 0 of 3 diverge (also with `MC_ENGINE1995=1`) |
| `level12` (1996) with `MC_ENGINE1995=1` | check that the switch really changes something | diverges at tick 142, the same tick as the 1995 control did before |
| `ctest -C Release` (MC_NET_BIND=127.0.0.1) | all gates incl. reference_test, reference_levels, reference_player, reference_gen, replay_check | **60 / 60 pass** |

Commands (from the repository root):

```
set MC_HIDDEN_DIR=C:\Magic Carpet\extracted\gog_cd\CARPET
build\Release\reference_test.exe MagicCarpet\magic extracted\reference\hw_level00     (likewise 05 09 12 18 24)
set MC_ENGINE1995=1
build\Release\reference_test.exe MagicCarpet\magic extracted\reference\cd95_level12
```

About the new recordings: levels 5, 18 and 24 have matching record and play runs (level 24 differs at
one rec_tick dump). On level 9 every `rec_tick*.gam` differs from the play dump, but only in
per-instance fields (`norm_state.py rec_tick00500.gam tick00500.gam`: "no difference in the hashed
state"). Those are the same record-vs-playback artefacts as in 1996. The play run is the reference.

## The differences implemented

"1996" = carpet.exe address (the port's names), "CD" = CD CARPET.EXE address. Every entry was read
from the disassembly of both exes (scratchpad `re_hw/cmpv.py`, `cmpr.py`, `d95.py`). The jump tables
were compared separately (`re_hw/jtcmp.py`), because the token compare of `cmp96.py` masks their
contents.

### AI wizard (`ai_wizard.cpp`)

| function (1996 / CD) | 1995 behaviour | evidence |
|---|---|---|
| `ai_approach_target` 0x140d0 / 0x15470 | Within `near`: stop (as 1996). Otherwise, **while speed-up (spell 2) is active, return 0 and set nothing**. Beyond `far` with speed-up ready, cast it and return 0. Otherwise cruise (`target_speed = speed_base`, accelerating). 1996 cruises whenever `d <= far`, so the speed-up's target speed 0xa0 is overwritten by 0x50 at once. | the tick-3 divergence of hw_level00 (speed_cur 160 vs 80, P+0xc 0xa0 vs 0x50, P+0xe 0 vs 1) |
| `ai_mode12_idle` 0x12680 / 0x13a10 | speed-up active: return 0. Ready: cast, return 0. Otherwise cruise, return 0. 1996 casts first and returns 1 while the spell runs. | CD 0x13a15 `ai_spell_active(2)` first |
| `ai_mode11_return_home` 0x126f0 / 0x13a70 | Its no-castle tail is the same 1995 idle sequence (CD 0x13b45). The castle branch is unchanged. | |
| `hover_toward` (tails of modes 1, 3, 6, 7, 8) | **No `target_speed = 0`**. Only the z step. | 1996 `mov word [P+0xc],0` at 0x124f8, 0x125b2, 0x128f4, 0x12a28, 0x12b7e. None in CD. |
| `ai_mode3_fly_to_castle_site` 0x12560 / 0x138f0 | far distance 0xc00 (1996 0x1000) | CD 0x1390a `push 0xc00` |
| `ai_mode8_attack_wizard` 0x12a90 / 0x13dd0 | near / far 0xc00 / 0x1000 (1996 0xd00 / 0x1200) | CD 0x13e27 |
| `ai_mode6_collect_mana` 0x12830 / 0x13ba0 | After a successful possession cast (and the claim test) it falls through to the hover and returns 1. 1996 returns 0 at once. | CD 0x13c25 / 0x13c57 |
| `ai_goal_upgrade_castle` 0x12df0 / 0x14120 | Inlined test: castle exists, castle spell owned, not running (`cast_ticks == 0`), cooldown 0, footprint clear, `mana_total >= spell.mana_total`, castle state 4. No `castle.duration == 0` test, no aim test (1996 calls `ai_castle_spell_ready_14980`). | CD 0x14164..0x141e2 |
| `ai_spell_ready` 0x14640 / 0x15a00 | Jump table for 0..0x14. **Fire Wall (0x14) uses the aimed test** of 3 / 7 / 8 / 0x11: not running, cooldown 0, mana, aim tolerance. 1996 sends 0x14 to the generic test. | table CD 0x159ac, entry 0x14 -> 0x15aa1 |
| `ai_cast_spell` 0x14240 / 0x155f0 | Jump table for 0..0x14: 0x12 / 0x13 -> 0, **0x14 an aimed single cast** (as 3 / 7 / 8 / 0xb / 0xd / 0x11). Speed-up (2) also needs `cast_ticks == 0` (CD 0x158c7). | table CD 0x15590 |
| cooldown reload table 0x938c4 / 0x90034 | Entry 0x14 is 4 (1996: 1). It is the only differing entry, and only the 1995 code casts 0x14. The constant is in `ai_cast_spell`. | `re_hw/tabchk.py`: the only port table that differs |
| `ai_has_any_attack_spell` 0x154e0 / 0x16920 | also true for Fire Wall (0x14) | CD 0x16961 |
| `ai_choose_attack_spell` 0x14c70 / 0x16030, `ai_choose_castle_attack_spell` 0x14f00 / 0x16310 | A Fire Wall rung after meteor (7) and before fireball (0), with the same ready / affordable-off-cooldown logic as the other rungs | CD 0x1623b, 0x1646d |
| `ai_counter_projectile` 0x15460 / 0x16890 | Projectile type 0x10 (Fire Wall) is countered like 0 / 3 (rebound, else shield). 1996 has a jump table for 0..9. | CD 0x168c7 |
| `ai_record_threat_from_projectiles` 0x150f0 / 0x16540 | Type 0x10 counts as heavy (+5000 against castles, +3000 against wizards / balloons) | CD 0x165f5, 0x16704 |

### Castle (`castle.cpp`, `effects.cpp`)

| function | 1995 behaviour | evidence |
|---|---|---|
| `castle_spill_mana` 0x41720 / 0x47130 | After the mana-ball loop: **four class-10 type-0x36 effects** at the castle, each owned by the castle's owner and moved by `math_rotate_offset(pos, t->rng & 0x7ff (one LCG step each), 0, 0x1900)`. The loop itself is unchanged. | CD 0x4735e..0x473ca. Was the hw_level12 tick-269 divergence (effect 54/59 vs mana ball, castle rng). |
| `effect_castle_raise_terrain_s44` 0x26f10 / 0x285c0 | When it hands the castle back it sets `cast_ticks = 5` (1996: 2). The castle then runs build step 5 (level the ground, effect 0x29), which writes 2 itself. | CD 0x28cc4 `mov word [...+0x7493], 5`. Was the hw_level12 tick-20 / cd95_level12 tick-142 divergence (castle cast_ticks 5 vs 2, castle spell cast_ticks 100 vs 0). The function is not in map96.csv. It was found through the parallel Table A (`re_hw/tabmap.py`). |

### Creatures (`creatures2.cpp`, `creatures3.cpp`)

These are also unmapped in map96.csv and were found through Table A (`tabmap.py` maps every
dispatch-table handler by its record).

| function | 1995 behaviour | evidence |
|---|---|---|
| `creature_kraken_s38_update` 0x1b000 / 0x1c4f0 | `speed_cur = 0x1e` on entry | CD 0x1c501 |
| `creature_kraken_s39_update` 0x1b390 / 0x1c880 | `speed_cur = 0x1e` after `creature_follow_leader` | CD 0x1c893 |
| `creature_genie_s67_update` 0x1caf0 / 0x1dfe0 | Both player-list searches (the enemy in sight / fov, and the "first thing with mana" target) only consider wizards (player types 0 / 1) | CD 0x1e1ab, 0x1e311 |

`creature_kraken_s37` differs only in register allocation.

### Spells (`spells.cpp`)

| what | 1995 behaviour | evidence |
|---|---|---|
| Table A class 12 record 68 (phase 2 of spell 22) | Bound to the phase-2 pickup wrapper (CD 0x56260 = `spell_phase2_common_47300`). 1996 binds it to the cast handler 0x49140, so in 1996 a level pickup of spell 22 can never be picked up. Port: `spell_update_shared` dispatches state 0x44 to `spell_phase2_common` under `engine1995()`. | CD record 0x98790: handler 0x56260. HIDDEN: the same (`re_hw/dispcmp.py`). That is the only 1996-vs-CD difference in all 14 classes' Table A / B (index, handler, enabled). |

### Player (`player.cpp`)

| function | 1995 behaviour | evidence |
|---|---|---|
| `player_apply_controls` 0x40e70 / 0x46840 | Prelude: if `P->input_bits == 0x30` exactly (fire left + fire right and nothing else), the own castle (if any) gets `health = -1` and **no controls are applied** that tick. The 1995 `player_local_input` also has a debug key (scancode 0x26) that queues exactly this, and a key 0x25 that sets the wizard's health to -1 outside network games (CD 0x1804d / 0x18070). The rest of the function differs only in register allocation and data addresses. | CD 0x46848..0x4688c |
| `player_commands_process` 0x3a8b0 / 0x3c9d0, chat command 0x13 | The cheat-gate word is **"QUICK"** (1996: "RATTY"). | CD 0x3d187..0x3d28a compare 'Q' 'U' 'I' 'C' 'K' (upper or lower case) |

**Consequence for play:** pressing both fire buttons in the same tick while no movement key is held
destroys a level of your own castle in Hidden Worlds. That is what HIDDEN.EXE does. It is faithful, but
it may come as a surprise. If it should be optional, gate that one branch separately.

## Checked and not implemented (no simulation effect, or outside the port's 1996-derived input layer)

* `creature_follow_leader` 0x18e90: 1996 adds a `vga_set_dac_entry(0, 0x3f, 0, 0)` debug call (the port
  has it as a TODO comment). No state.
* `game_tick_update` 0x32e80: 1996 adds the screenshot / `debug_screenshot_3ca00` handling. No sim state.
* `level_load_file` 0x3d160: memset of 0x6a instead of 0x64 bytes at GameState+0x38c9f. This is the 6-byte
  save trailer of the 1995 GameState, which the port does not have.
* `level_load_levels_dat`, `cd_check_files`, `file_missing`, `init_early`, `config_parse`, the
  `fe_*`, `music_*` / `sound_*` / `snd_*`, `tmaps_load`, `cpu_detect`, `timer_isr_install`, CRT, VGA and
  `poly_fill_triangle` entries: file paths (C:\CARPET.CD\), front end, drivers, rasteriser.
* `render_frame` and the `ui_*` HUD functions: drawing only. Not ported. The 1995 HUD may look slightly
  different (status bars, player list, thing labels).
* `player_queue_command` 0x17270 / 0x188a0: identical except that 1995 ignores command 5 (jump table
  entry 5 -> the no-op). Nothing queues command 5.
* `player_commands_process`: besides the cheat word, 1995 copies 0xc bytes of network player names
  (1996: 0xa) and terminates at name+12. Network-only, and the port's PlayerRec name handling was
  left as is. The rest is register allocation.
* `player_local_input` 0x16660 / 0x17c20 and `player_function_keys` 0x156b0 / 0x16b00: the 1995 key map
  differs (debug keys 0x25 / 0x26 above, cheat F-key arguments shifted, function-key toggles). The
  port's input layer is its own translation of the 1996 one with modern bindings (docs/CONTROLS.md),
  so these were not changed.
* The 237 1996 functions without a map96 entry: every dispatch-table handler among them is now mapped
  and compared (`tabmap.py`). The rest are tiny stubs (`ai_set_mode*`, `ai_goal_disabled`,
  `castle_build_seq_set_done`, `stub_return_zero`, found byte-identical or token-identical in CD),
  `terrain_max_drop_around_2376f` (token-identical at CD 0x24e1f), debug / CRT / driver code.
* Port data tables (`src/mcengine/tables/*.tables`, generated from the 1996 exe): `re_hw/tabchk.py`
  finds every simulation table byte-identical in CD (sprite descriptors with the pointer words
  masked), except the cooldown reload entry above.

## Left divergent

Nothing in the references. All six Hidden playbacks, both cd95 playbacks (with `MC_ENGINE1995=1`) and all
generations match over their full length. Not covered by any reference: the human player's own
inputs (no scripted-player recording), the input-layer differences listed above, and the HUD.

## Scripts (session scratchpad `re_hw/`, not in the repository)

`cmpv.py NAME` (context diff 1996 vs CD of a mapped function), `cmpr.py a96 e96 acd ecd` (the same for
two ranges), `d95.py cd|hid|96 start end` (disassembly with call names), `jt.py` (jump-table dump),
`tabmap.py` (function mapping through the parallel dispatch tables + compare), `dispcmp.py` (Table A /
B records 1996 vs CD / HIDDEN), `jtcmp.py` (jump tables of all mapped pairs), `tabchk.py` (port data
tables 1996 vs CD / HIDDEN).
