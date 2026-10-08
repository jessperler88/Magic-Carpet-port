# Hidden Worlds (HIDDEN.EXE) vs CD CARPET.EXE: complete behaviour diff

Inputs: the user's GOG copy, `extracted/gog_cd/CARPET/CARPET.EXE` (1995 CD base game, "CD") and
`HIDDEN.EXE` (1995 Hidden Worlds, "HID"). Same Watcom LE layout (code object 0x10000, data object
0x90000). Function names are the 1996 `carpet.exe` names (`ghidra/export/carpet_functions.csv`) that
the port uses; "96 == CD" says whether the 1996 function is instruction-identical to the CD one (after
masking relocations and branch displacements), i.e. whether the port's current code is the CD
behaviour that HIDDEN changes.

Result in one sentence: apart from the data-file set and the level names, Hidden Worlds changes
**the terrain classification (a new snow pass, steep cells become class 1)**, **the Fire Wall spell
(spell 0x14) end to end** (spell stats, launch, projectile type 0x10, its new per-tick handler,
targeting, rebound, hit statistics and the impact effect type 0x35), **the tree constructor**, removes
the **campaign level skipping**, and moves the **campaign end (25) and multiplayer map count (10)**.
Nothing else in code or initialised data differs.

## 1. Summary table

| id | what | HID addr | CD addr | 1996 / port function | port file | 96 == CD |
|---|---|---|---|---|---|---|
| T1 | terrain_build calls the new snow pass after terrain_mark_steep | 0x31ae0 (call at 0x31bc8) | 0x31aa0 | terrain_build_303f0 | terrain_gen.cpp:487 | yes |
| T2 | terrain_mark_steep: steep cells get class **1** (not 6); its second pass becomes a no-op | 0x33570 | 0x33180 | terrain_mark_steep_31ad0 | terrain_gen.cpp:311 | yes |
| T3 | **new** terrain_mark_snow(snlin, snflt): high flat land gets class 6 | 0x31c10 (0x388 bytes) | - | - (new) | terrain_gen.cpp | - |
| E1 | effect type 0x35 / state 0x3a update: the meteor-impact shock ring, aux modulo **7** | 0x29780 | 0x29780 | effect_fire_pillar_s58_update_280d0 | effects.cpp:1107 (body = effect_meteor_update, effects.cpp:381) | yes |
| E2 | effect type 0x35 constructor: life 6, damage 3000, no yaw/flag 1/extents | 0x3bc60 | 0x3b8e0 | effect_create_fire_pillar_39b80 (port: effect_create_type53) | constructors.cpp:1150 | yes |
| P1 | projectile type 0x10 constructor: MoveDesc 0x96a50 (index 2) and sprite 0x4c | 0x3a5f0 | 0x3a270 | projectile_create_fire_wall_38510 (port: projectile_create_type16) | constructors.cpp:910 | yes |
| P2 | class 9 state 0x11 handler -> **new** function: fly + explosion trail | 0x54600 (new); Table A record handler field at data 0x97830 | 0x53060 | projectile_update_shared_44a40 (record 9/17) | projectiles.cpp:300 / 842 | yes |
| P3 | rebound test also accepts impact type 0x35 (3 inlined copies) | 0x52bfe, 0x5354e, 0x5424e | 0x528be, 0x5320e, 0x53f0e | fly() in projectile_fly_and_impact_44150 / projectile_type8_s8_update_44aa0 / projectile_thunderbolt_s12_update_457a0 | projectiles.cpp:116 | yes |
| P4 | hit statistics also count projectile type 0x10 | 0x52a00 | 0x526c0 | projectile_record_hit_stats_440a0 | projectiles.cpp:163 | yes |
| P5 | auto-aim: type 0x10 gets its own case, cone 0x100 instead of 0x71 | 0x548b0 (case at 0x54bb0) | 0x54520 | projectile_pick_target_45f00 | projectiles.cpp:642 | yes |
| S1 | Fire Wall spell constructor: duration 0x1a, cost 0xea60, damage 0x1388 | 0x3c730 | 0x3c3f0 | spell_create_fire_wall_3a690 | constructors.cpp:1460 (kSpells[20]) | yes |
| S2 | Fire Wall spell update: single launch (no burst loop), aim point 0x2800 ahead, sound 0xf | 0x58270 | 0x57d40 | spell_fire_wall_s60_update_48de0 | spells.cpp:317 / 705 | yes |
| C1 | tree constructor: always sprite 0x53, one RNG draw fewer | 0x37f80 | 0x37bc0 | scenery_create_tree_35e60 | constructors.cpp:189 | yes |
| G1 | campaign level skipping removed (level_skip_number deleted, call removed) | game_main 0x34470 (no call after 0x345bf) | 0x34070, call at 0x34201 | level_skip_number_329c0 / game_main_32a00 | game.cpp:139 / 160 | yes |
| F1 | multiplayer map list scrolls over 10 maps (was 20) | 0x4daec | 0x4d7ac | fe_screen_multiplayer_54bd0 | frontend.cpp:1897 | yes |
| F2 | campaign complete when Config.level == **0x19** (was 0x32) | 0x4f187 | 0x4ee47 | fe_screen_level_result_55b00 | frontend.cpp:1697 | yes |
| F3 | title flics intro\title-03.dat / title-04.dat (were -01 / -02) | strings 0xa9bdc, 0x4a414 | 0xa9bc4, 0x4a0d4 | fe_screen_title_56730 / fe_flic_loop_frame_56670 | frontend.cpp:1465, 1469; fli.cpp:60 | yes |
| D1 | data-file set 1 (blk1, pal1, build1, tmaps1, sky1-0, mspr1, hspr1, ddlevels) **and data/dtables.dat** | strings, section 5 | | | world_set.cpp:21-34, tables.cpp:156, engine.cpp:22 | yes |
| D2 | level names: 25 campaign names, MP names 51..60 replaced | pointer table 0x999b8 | 0x999b8 | fe_level_name_ptrs (1996 0x97490) | world_set.cpp (already read from HIDDEN.EXE) | yes |
| I1 | install/setup copy mask "levels" -> "ddlevels" (cd_check_files) | 0x3f659 | 0x3f319 | cd_check_files_3cf20 | none (port has no installer) | no (install code differs 96/CD) |

Everything else in the code object (0x10000..0x7c0ae / 0x7c5be), object 2, object 4 and the data
object is identical once code shifts (+0x34 .. +0x510), the string-pool shifts (+4 .. +0x18 in
0xa9310..0xabb20) and the BSS shift (-0x10 above 0xac1a0: GameState pointer 0xae400 -> 0xae3f0,
g_map_height 0xdc1e0 -> 0xdc1d0, g_map_flags 0xfc1e0 -> 0xfc1d0, g_map_type 0xcc1e0 -> 0xcc1d0,
cell scratch 0x10c1e0 -> 0x10c1d0) are accounted for. Functions the earlier n-gram matcher had
flagged but that are **identical** CD vs HID: render_build_roll_table_28580, render_landscape_29050,
render_sprite_scaled_2ad60, thing_update_all_3dce0, config_parse_33750, castle_build_update_41500,
castle_set_level_stats_42200, player_commands_process_3a8b0, sound_request_49720 and the
CRT / driver / UI ones in `fdiffs/` (they differ 1996 vs CD, not CD vs HID; see Appendix A).

## 2. Method and coverage (so the "nothing else" claim can be checked)

* `re_hw/align.py`: linear disassembly of both code objects with LE fixups masked, jump tables
  (consecutive code fixups) as data, one global difflib alignment (136149 / 136619 instructions),
  then every aligned instruction's relocated operand and branch target checked through the code map
  and the data-delta map. 226 non-equal blocks; each one is listed in the table above or is
  data-in-code / padding (music_stop 0x20f0c, jump tables at 0x3c934 / 0x404ef / 0x43aff / 0x4865f,
  the front-end data block 0x49b00..0x4a7d0 holding fe_menu_items, the sound-card table and
  intro\title-02.dat).
* `re_hw/fxcheck.py`: all 32-bit fixups of the CD code object mapped into HIDDEN (target compared
  under the maps); all mismatches explained above.
* `re_hw/ddiff2.py` / `strrefs.py` / `nonstr.py` / `cdata.py`: the data object byte by byte under the
  delta map, fixups compared by mapped target, every code/data reference into the string pool compared
  by string content, non-string bytes of the string pool diffed separately, strings embedded in the
  code object diffed. The only non-string data change is the Table A handler of class 9 state 0x11
  (P2); the rest is file names, level names, the build time ("16:26:18" -> "16:16:43"), sprite debug
  names and filler bytes between strings (0xa4 -> 0xcc at 0xa8f07 / 0xa8fb3 etc.).
* Ghidra 12.0.4 project `ghidra/project_hw/HW` (HIDDEN.EXE, CARPET.EXE, Watcom cspec, analyzers as
  in the memory notes) with `re_hw/gscripts/HwDecomp.java` for the functions Ghidra did not create.
* Verification by emulation (`re_hw/emu_terrain.py`, unicorn): the HIDDEN functions 0x33570 + 0x31c10
  on random maps equal the Python of section 3.3; the complete HIDDEN terrain_build (0x31ae0) on
  DDLEVELS entries 0, 3, 7, 12, 18, 24 equals the CD terrain_build (0x31aa0) with its
  terrain_mark_steep call replaced by `mark_steep_hw(rkste); snow(snlin, snflt)` (height, flags and
  type maps byte-identical). `re_hw/emu_tables.py`: HIDDEN's tables generator fed pal1-0 reproduces
  DTABLES.DAT (section 5.2).

(`re_hw/...` = the session scratchpad
`%TEMP%/claude/C--Magic-Carpet/809e1acd-.../scratchpad/re_hw`; copy the scripts if they are to be kept.)

## 3. Terrain

### 3.1 T1 terrain_build (HID 0x31ae0)

Identical to the port's `terrain_build` except one inserted call; the GenMap fields are pushed as
zero-extended u16 (`xor eax,eax; mov ax,[ebx+off]`):

```c
    terrain_mark_steep(gen.rkste);                    // HID 0x33570, modified (T2)
    terrain_mark_snow_31c10(gen.snlin, gen.snflt);    // NEW (T3): (u16)[level+0x1c], (u16)[level+0x20]
    terrain_flags_fill_holes();                       // unchanged
    memset(g_map_type, 0, 0x10000);
    terrain_smooth_spikes(); terrain_fix_shore_quads(); terrain_assign_textures();
    terrain_mark_water_anim(); terrain_build_lightmap();
```
`gen.snflt` is still also passed to terrain_classify_flat earlier (unchanged). The mc_types.h comment
"snlin ... not used" holds for the base game only. Only terrain_build calls the two passes.

### 3.2 T2 terrain_mark_steep (HID 0x33570)

```c
void terrain_mark_steep_hw(int min_range) {
    memcpy(g_map_type, g_map_flags, 0x10000);                 // scratch, as in CD
    for (unsigned i = 0; i < 0x10000; ++i)
        if (g_map_flags[i] != 0 && cross_range(x, y) >= (uint8_t)min_range)
            g_map_flags[i] = 1;                               // CD: 6
    // The second pass is still there but now selects cells == 1 and writes 1 (counters for 3 / 2 /
    // 5 / 4 with the same decision as CD): it changes nothing and can be omitted.
}
```
So in Hidden Worlds steep land is class 1 ("cliff edge") and class 6 is free for snow.

### 3.3 T3 new terrain_mark_snow_31c10(snlin, snflt) (HID 0x31c10..0x31f97)

Register-level structure is a copy of terrain_mark_steep: `ax` walks all 65536 cells (al = x, ah = y,
neighbours by byte inc/dec, so the map wraps), no RNG, no tables. Arguments on the stack:
`[esp+0x10]` = snlin (only the low byte is compared), `[esp+0x14]` = snflt (low byte).

```c
void terrain_mark_snow_31c10(uint16_t snlin, uint16_t snflt) {
    memcpy(g_map_type, g_map_flags, 0x10000);        // 0xfc1d0 -> 0xcc1d0; dead (g_map_type is
                                                     // cleared two calls later), keep for fidelity
    // pass 1: high and flat -> class 6
    for (unsigned i = 0; i < 0x10000; ++i) {
        if (g_map_height[i] <= (uint8_t)snlin) continue;             // unsigned byte compare (jbe)
        int range = cross_range(i & 0xff, i >> 8);                   // max - min of the 5-cell cross
                                                                     // (self, N, E, S, W), as port
        if (g_map_flags[i] != 0 && range < (int)(uint8_t)snflt)      // whole byte != 0; signed compare
            g_map_flags[i] = 6;
    }
    // pass 2: snow touching low land becomes class 1 (same rule as CD mark_steep pass 2, target 6)
    for (unsigned i = 0; i < 0x10000; ++i) {
        if (g_map_flags[i] != 6) continue;
        int c3 = 0, c2 = 0, c5 = 0, c4 = 0;          // over the 8 neighbours (x,y-1),(x+1,y-1),(x+1,y),
        for (each of the 8 neighbours n) {           // (x+1,y+1),(x,y+1),(x-1,y+1),(x-1,y),(x-1,y-1)
            c3 += g_map_flags[n] == 3;  c2 += g_map_flags[n] == 2;
            c5 += g_map_flags[n] == 5;  c4 += g_map_flags[n] == 4;
        }
        bool set = c3 ? (c2 || c5 || c4) : (c2 || (c5 && c4));
        if (set) g_map_flags[i] = 1;
    }
}
```
Both passes work in place; the result does not depend on the order (pass 1 reads only heights and the
cell's own flag; pass 2 only counts classes 2..5, which neither pass writes). Pass 2 is literally the
port's `terrain_mark_steep` second loop with `!= 6` kept. Effect seen in emulation: DDLEVELS entry 0
(snlin 5, snflt 50) ends with 64355 class-6 cells (almost all land is snow), entry 7 (snlin 45,
snflt 6) 205, entry 24 (snlin 119, snflt 21) 2108. Textures: terrain_assign_textures is unchanged;
the blk1-* textures supply snow for class-6 corner combinations (g_corner_class_tuples /
g_slope_tex_tables are identical in HIDDEN).

Consequences for unchanged code: everything that reads the class bits (`g_map_flags & 7`) now sees
"snow" where the base game saw "steep" and "cliff edge (1)" on steep slopes. That is automatic in a
faithful port; port-only code that interprets class 6 as rock must not be used for set 1.

## 4. The Fire Wall spell (spell 0x14, projectile 0x10, effect 0x35)

Hidden Worlds turns Fire Wall into a homing fire projectile that leaves an explosion trail and ends
in a small meteor-style shock ring. All pieces:

### 4.1 S1 spell_create_fire_wall (HID 0x3c730) - stack arguments of spell_create_common

| field (port SpellParams) | CD (= kSpells[20]) | HID |
|---|---|---|
| type, state | 0x14, 0x3c | same |
| total_mana | 0x1388 | 0x1388 |
| levels (-> Thing.duration, cast ticks) | 0x33 | **0x1a** |
| flags, unk3e | 1, 0 | same |
| cost (-> mana_cost +0x84) | 0x2ee0 | **0xea60** |
| damage (-> u16 Thing.damage) | 0x15f90 (stored 0x5f90) | **0x1388** |

Mana per tick = total_mana / duration = 5000 / 26 = 192 (CD 98).

### 4.2 S2 spell_fire_wall_s60_update (state 0x3c, HID 0x58270)

CD = `spell_cast_burst(t, launch_fire_wall)`. HID has no burst loop; it is exactly
`spell_cast_single(t, launch_fire_wall_hw, /*charge_every_tick=*/true)` (spells.cpp:76):

```c
void spell_fire_wall_s60_update_hw(Thing *t) {
    if (t->cast_ticks <= 0) return;
    Thing *c = &things[t->caster];                         // +0x2a
    if (c is none (<= things base)) { t->cast_ticks--; return; }
    if (!spell_can_cast(t, c)) { t->cast_ticks = 1; t->cast_ticks--; return; }
    if (t->cast_ticks == t->duration) launch_fire_wall_hw(t, c, thing_pos(c));
    spell_charge_mana(t, c);
    t->cast_ticks--;                                       // Thing.burst is not touched
}
void launch_fire_wall_hw(Thing *t, Thing *c, const Pos *pos) {
    // = launch_fireball_like(t, c, pos, 0x10, 0x35, sound) with two constants changed
    Thing *p = thing_create(pos, 9, 0x10); if (!p) return;
    shot_add_caster_speed(p, c); spell_projectile_origin(c, p); shot_set_impact(p, 10, 0x35);
    p->owner = c->owner; shot_raise(p, c); p->mana = t->mana; p->damage = t->damage;
    shot_take_aim(p, c);
    shot_target_ahead(p, c, c->pitch, 0x2800);             // CD 0x4000
    shot_copy_angles(p, c);                                // (after the rotate in HID; no reads in between)
    sound_request((int16_t)thing_index(p), -1, 0xf);       // CD sound 9
}
```
The store order differs from CD only between independent field writes. Behavioural difference from
the burst loop: none while Thing.burst == 0, which is always the case for this spell (unk3e == 0,
input.cpp:561 only queues bursts for unk3e != 0).

### 4.3 P1 projectile_create_type16 (HID 0x3a5f0)

`projectile_create(pos, 0x10, 0x11, 0x180, 0x2000, desc, sprite)` with desc **0x96a50** (1996
address; MoveDesc index 2, the homing desc of types 1 / 0x11: yaw step 0x71, pitch step 0x71;
CD 0x96ab0 = index 5: yaw 5, pitch 0x16) and sprite **0x4c** (CD 0x2a; 0x4c is projectile type 3's
sprite). In CD-image addresses: [t+0x9c] = 0x98f78 instead of 0x98fd8 (table base 0x98f38 in both).

### 4.4 P5 projectile_pick_target (HID 0x548b0)

Jump table (HID 0x5485c, by Thing.type 0..0x13) sends type 0x10 to a new case (0x54bb0) instead of
the 0/3/4/0x10/0x12/0x13 group. The new case is the group case (port lines 674-704) with
`cone = 0x100` instead of 0x71 in all three score calls:

```c
    case 0x10: {
        const unsigned cone = 0x100;                         // group case: 0x71
        ... players: range = owner MoveDesc sight_radius; skip same owner / flags & 0x20;
            s = o->type == 2 ? target_aim_score(t, o, cone, 0x71) : projectile_target_score(t, o, cone, 0x71);
        ... creature lists 0..19: skip same owner / timer_a == 0; s = projectile_target_score(t, o, cone, 0x71);
        if (!found) return 0;
        t->target = thing_index(found); thing_aim_at(t, found);
        if (found->cls == 3 && found->type == 0) player_set_combat_music_timer(found);
        return 1;
    }
```
All other cases are instruction-identical apart from register allocation. With fly()'s first-tick
aim this makes the Fire Wall projectile pick a target in a wider cone and (P1) steer at 0x71 per tick.

### 4.5 P2 new class-9 state-0x11 handler (HID 0x54600)

Table A record class 9 index 17 (data 0x9782a: {0x968f4, 17, handler, 1}) points at 0x54600 in HID
(CD: 0x53060 = projectile_update_shared_44a40 = fly_and_impact, still used by states 2, 4, 5, 6, 11,
15, 16, 20). The port binds handlers by original address (projectiles.cpp:842 binds 0x44a40 to all
of them), so the HW switch must rebind record (9, 0x11) only.

```c
void projectile_fire_wall_update_hw_54600(Thing *t) {
    projectile_fly_and_impact(t);                         // HID 0x52ab0 (with P3, P4)
    if (t->cls != 0) {                                    // byte [t+0x40]; true unless the slot was freed
        Thing *e = thing_create(thing_pos(t), 10, 0);     // explosion (effect_create_explosion_38730)
        if (e) {
            e->flags |= 0x10080;                          // or byte [e+0x10],0x80; or byte [e+0x12],1
            e->owner = t->owner;
        }
    }
}
```
One explosion per tick at the projectile's position after the move, including the impact tick (the
projectile is only flagged 0x400 by thing_mark_delete). Same flags as the meteor ring's explosions
(no area damage of their own). No RNG.

### 4.6 P3 rebound (fly(), projectiles.cpp:116)

```c
        if ((hit->flags & 0x8000) && t->mana / 4 <= hit->mana && t->impact_cls == 10 &&
            (t->impact_type == 1 || t->impact_type == 0x11 || t->impact_type == 0x35)) {   // HW adds 0x35
```
Changed identically in all three inlined copies (44150, 44aa0, 457a0), i.e. once in the port's fly().
The rest of those three functions differs only in register allocation.

### 4.7 P4 projectile_record_hit_stats (HID 0x52a00)

Counted types become `0, 1, 3, 7, 8, 9, 0x10, 0x13` (CD: without 0x10). Body unchanged
(PlayerBlock +0x157 shots, +0x15b hits).

### 4.8 E2 effect_create_type53 (type 0x35 -> state 0x3a, HID 0x3bc60)

```c
Thing *effect_create_type53_hw(const Pos *pos) {      // = effect_create_meteor_38f10 with life 6
    Thing *t = thing_alloc(); if (!t) return nullptr;
    t->state = 0x3a; t->cls = 10; t->type = 0x35;
    set_pos_raw(t, pos);                               // movsd/movsw into +0x48
    t->max_health = 6;                                 // CD 0x80
    t->damage = 3000;                                  // CD 100
    t->flags &= ~8u;
    thing_restore_health(t);
    return t;                                          // CD also: speed_cur 0x100, aux 0, yaw = rng & 0x7ff
}                                                      // (one Thing-RNG draw), flags |= 1, extents 0x400 x 0x4000
```
No RNG draw in HID. projectile_fly_and_impact then overwrites e->damage with the projectile's damage
(5000 from S1) and copies owner / yaw / pitch / target as before.

### 4.9 E1 effect_fire_pillar_s58_update (state 0x3a, HID 0x29780)

The HID body is instruction-identical to effect_meteor_s17_update_24630 (CD 0x25ce0) except the
modulus 0xb -> **7**. Port: effects.cpp:381 with `% 7`:

```c
void effect_fire_pillar_update_hw(Thing *t) {
    if (life_expired(t)) { thing_mark_delete(t); return; }      // h = health--; dead when h < 0
    if (!(t->flags & 2)) { t->flags |= 0x10002u; sound_request((int16_t)thing_index(t), -1, 0x1e); }
    thing_set_extents(t, ((int32_t)t->aux * 3 * 256) / 4, 0x200); // = aux * 0xc0
    thing_area_damage(t, 0, (unsigned)(t->damage / t->max_health) & 0xffff);   // 5000 / 6 = 833
    SpiralSearch s; int dx, dy;
    if (spiral_search_begin(&s, (int16_t)t->aux, (int16_t)t->aux)) {           // ring `aux` only
        rng_next(t);                                              // Thing RNG (+4), unused draw
        while (spiral_search_next(&s, &dx, &dy) == 1) {
            Pos p = scatter_pos(t, dx, dy, 0xa0);                 // 2 Thing-RNG draws, x first, % 0x81
            Thing *c = thing_create(&p, 10, 0);                   // explosion
            if (!c) continue;
            c->owner = t->owner; c->yaw = t->yaw; c->flags |= 0x10080u;
            thing_set_extents(c, 0x200, 0x200); c->aux = 0;
        }
        spiral_search_end(&s);
    }
    t->aux = (int16_t)((int16_t)(t->aux + 2) % 7);                // CD meteor: % 0xb
}
```
Life 6 -> 7 updates with aux = 0, 2, 4, 6, 1, 3, 5, removed on the 8th. The CD pillar (15 ticks of
type-6 fires stacked 0x80 higher per tick, extents 0x200 x 0x800) is gone. All RNG here is the
per-Thing LCG at Thing+4 (x*0x24a1+0x24df); g_state->rng is not touched (thing_alloc seeds
Thing.rng = index + g_state->rng without advancing it).

## 5. Other gameplay

### 5.1 C1 scenery_create_tree (HID 0x37f80)

```c
Thing *scenery_create_tree_hw(const Pos *pos) {
    ... identical up to and including thing_restore_health(t) (3 Thing-RNG draws: health, x, y) ...
    thing_set_sprite(t, 0x53);            // thing_set_sprite_small_352d0; CD: (rng_next(t) & 1) ? 0x54 : 0x53
    return t;
}
```
The tree's own RNG state therefore ends one step earlier than in CD (it is per Thing, so only later
draws of that tree are affected).

### 5.2 G1 / F1 / F2 level selection, campaign end, multiplayer

* **G1** game_main no longer calls level_skip_number after the front end (CD skips campaign indices 8,
  0x11, 0x1c, 0x21, 0x27). In HID the function is deleted (CD 0x34070 -> padding at HID 0x3445f) and
  the call at CD 0x34201 removed; game.cpp:160 must not run for set 1.
* Level files: level_load_and_init (HID 0x40ae0) loads `levels/ddlevels.dat` entry `Config.level`
  (0-based) into GameState+0x2f503, same code as CD. The win path still does `Config.level++`
  (game_main HID 0x3477d). New campaign starts at 0.
* **F2** fe_screen_level_result: `state = (Config.level == 0x19) ? 10 /*outro*/ : 2` (CD 0x32). Equality
  test as in CD. So the campaign is DDLEVELS entries 0..24 played in order, then the outro (same
  intro\outro.dat).
* **F1** multiplayer lobby: scroll-down allowed while `scroll + 5 < 10` (CD 0x14), i.e. maps 0..9.
  Start still sets `Config.level = sel + 0x32`, names are `level_name(0x33 + i)`: HW multiplayer =
  DDLEVELS entries 50..59, names "Goyaan" .. "Halaj" (table entries 51..60, section 6).
* DDLEVELS content (70 entries): 0..24 new campaign, 25 new (unreachable from the campaign: the
  campaign ends when Config.level becomes 25), 26..48 byte-identical to LEVELS entries 26..48
  (unused in HW), 49..64 new (50..59 = the 10 MP maps; 49, 60..64 unreachable from the menus),
  65..69 identical to LEVELS. Only the command line / function-key paths (config_parse, the level
  cheat in player_function_keys) can reach the unused entries; those paths are unchanged code.
* Saves: same code and same name `%s%s\save\carpdd%02X.gam` in both exes; nothing marks a save as
  Hidden Worlds. Base and HW share the save slots and a save's Config.level is interpreted against
  whichever exe loads it. (The port's own indexing, HW_LEVEL_BASE = 100, avoids this; the original
  does not.)
* No other campaign bound changed (all other Config.level users are identical code).

### 5.3 F3 front end

* Title: fe_screen_title plays `intro\title-03.dat` (CD title-01) with the unchanged cue table
  (fli_cue_title01 contents identical); the attract loop loads `intro\title-04.dat` (CD title-02;
  string in the code-object data block, HID 0x4a414). Both files are in the GOG INTRO folder.
* Level names (section 6), MP map count (F1), campaign end (F2). Menus, screens, fonts, sound-card
  tables, credits text (g_hud_credit_ptrs strings compared one by one), player-name pool, language
  screen: identical.

## 6. Data object and files

### 6.1 Files HIDDEN loads (string changes, all referenced)

| CD | HID | used by |
|---|---|---|
| data/blk0-0.dat, data/blk0-1.dat | data/blk1-0.dat, data/blk1-1.dat | resource list 0x996e0 / 0x99738 |
| data/sky.dat | data/sky1-0.dat | 0x99764 |
| data/pal0-0.dat (x2) | data/pal1-0.dat | resource list 0x99528, 0xa8f1c (colour lookup init) |
| data/build0-0.dat / .tab | data/build1-0.dat / .tab | 0x99344 / 0x99370 |
| data/tmaps0-0.tab, "tmaps0-0" (x2) | data/tmaps1-0.tab, "tmaps1-0" | 0x9944c; tmaps_load (HID 0x58cf4 / 0x58d36), cd_check_files |
| data/mspr0-0.dat/.tab, data/hspr0-0.dat/.tab | mspr1-0, hspr1-0 | 0x99814.., 0x998f0.. |
| levels/levels.dat/.tab, %s%s/%s/levels.dat/.tab | levels/ddlevels.*, %s%s/%s/ddlevels.* | level_load_levels_dat (HID 0x3e440), cd_check_files |
| **data/tables.dat** | **data/dtables.dat** | tables_load_or_generate (HID 0x447b0): load at 0x44870 and the save after regeneration at 0x44a1a |
| intro\title-01.dat, intro\title-02.dat | intro\title-03.dat, intro\title-04.dat | F3 |
| setup mask "levels" | "ddlevels" | cd_check_files (install only) |

Unchanged (shared with the base game): fonts, pointers, e/f/g/itext, `data/music%d-%d`, `data/snds%d-%d`
(and the music0-0 / snds0-0 list entries), screens\*, smatitle.*, inst/drum.bnk, search.dat,
intro\intro/outro/scroll/levelw1/levelw2/levelose/logo/intel, sndsetup.*, language.inf, save names,
lev/gam/map/mvi%05d. **No code path in HIDDEN opens a set-0 terrain/palette/sprite/level/tables
file**; the only base-named strings left are the shared ones above and the error message text
"ERROR decompressing levels.dat".

### 6.2 dtables.dat (port gap)

HIDDEN loads and, if missing, regenerates and saves `data/dtables.dat` (0x14600 bytes, image at HID
0xb7924, CD 0xb7934). GOG DTABLES.DAT is stored unpacked (83456 bytes); TABLES.DAT is RNC (39339).
They differ in 19470 bytes because the shade / blend tables are built from the palette: emulating
HIDDEN's generator with pal1-0 reproduces DTABLES.DAT exactly in 0x0000..0x14000 (shade + blend) and
0x14300..0x14600 (circle profile); the texture-average block 0x14000..0x14300 comes from the set-1
textures and was not regenerated in the test. The port's engine.cpp:22 comment ("HIDDEN.EXE loads
data/tables.dat too") is wrong: for set 1, load `data/dtables.dat` (raw or RNC) into g_tables_image,
or regenerate from pal1-0 + blk1-*, and reload it on world_set_select (engine_world_set_changed does
not reload tables today). world_set.cpp's redirect list also lacks tables.dat -> dtables.dat and the
two title files.

### 6.3 Non-string data

* The single pointer change: Table A class 9 record 17 handler (data 0x97830), section 4.5.
* No numeric table changed: every table in `src/mcengine/tables/*.tables` is identical (earlier
  tabcmp), and the full byte diff of 0x90000..0xa9310 and 0xac1a0..0x132c00 (no shift) shows only
  file-name characters and filler bytes; the string pool 0xa9310..0xac1a0 contains no non-string data
  that differs (padding only). Object 2 (0x80000) and object 4 (0x140000) are identical. Code-object
  data (fe_menu_items, sound-card tables, lobby tables, jump tables) is identical up to code shifts.
* Level-name pointer table at 0x999b8 (entry 0 empty, same address in both): entries 1..25 =
  "1. Goyaan", "2. Cyrecius", "3. Esyphium", "4. Barabban", "5. Tervilar", "6. Imnara", "7. Kouppi",
  "8. Fayaoud", "9. Rama'Q", "10. Halaj", "11. Uzurmin", "12. Dhellea", "13. Nimahm", "14. Buhrghrok",
  "15. Sendel", "16. Cassida", "17. Illyum Gar", "18. Haj Vonor", "19. Enzanidor", "20. Adra Geria",
  "21. Krethe", "22. Wassat", "23. An Jafud", "24. Axmuhl", "25. Abnasur"; 26..50 keep the base names
  (unused); 51..60 = "Goyaan", "Cyrecius", "Esyphium", "Barabban", "Tervilar", "Imnara", "Kouppi",
  "Fayaoud", "Rama'Q", "Halaj" (base: Bussorah .. Phiria); 61..70 unchanged (Moussul .. Comari,
  not reachable in HW). The port already reads these from HIDDEN.EXE (world_hidden_level_name) but
  only 1..25; the MP names 51..60 are needed too for the lobby (frontend.cpp:1736).

## 7. What the port needs (by file), behind the Hidden Worlds switch

* terrain_gen.cpp: `terrain_mark_steep` writes class 1 (skip its second loop or keep it as no-op);
  add `terrain_mark_snow_31c10(gen.snlin, gen.snflt)` after it in `terrain_build` (3.3).
* effects.cpp: state 0x3a handler = `effect_meteor_update` body with `% 7` (4.9).
* constructors.cpp: `effect_create_type53` (4.8), `projectile_create_type16` desc 0x96a50 / sprite 0x4c
  (4.3), `kSpells[20]` duration 0x1a / cost 0xea60 / damage 0x1388 (4.1), `scenery_create_tree` (5.1).
* projectiles.cpp: fly() rebound + 0x35 (4.6), record_hit_stats + 0x10 (4.7), pick_target case 0x10 with
  cone 0x100 (4.4), new state handler for record (9, 0x11) (4.5) - the dispatch binding must be per record.
* spells.cpp: Fire Wall update = spell_cast_single + launch with 0x2800 / sound 0xf (4.2).
* game.cpp: no level_skip_number for set 1 (5.2).
* frontend.cpp: lobby scroll bound 10, campaign end at 0x19 (already via world_campaign_complete), MP
  names, title-03 / title-04 (5.3).
* tables.cpp / engine.cpp / world_set.cpp: dtables.dat (6.2).

The switch has to be part of the simulation state used by movies, saves and the reference harness
(all of the gameplay items change the per-tick state).

## 8. Open questions

1. **1995 engine vs 1996 engine** (Appendix A): HIDDEN is a 1995 build. 59 functions differ between the
   1996 exe (the port) and CD/HIDDEN, among them AI, player input / commands, castle_spill_mana,
   level loading, HUD drawing. These are not Hidden Worlds changes (CD and HIDDEN are identical there)
   but an "exact HIDDEN" mode would also need the 1995 versions. Done since: port_hidden_engine1995.md
   (`engine1995()`).
2. DDLEVELS entries 25, 49, 60..64 (new data) are unreachable from the menus; whether anything (the
   command line `level` option, SELECT.EXE) was meant to use them is unknown.
3. The texture-average block of dtables.dat (0x14000..0x14300) was not regenerated in the emulation
   check; loading the shipped DTABLES.DAT avoids the question.
4. The new P2 handler's `t->cls != 0` test: only false if the projectile's slot was freed inside
   fly_and_impact; with the port's 8192-Thing pool the recycling path differs anyway (keep the test).
5. Data files present on the GOG disc but referenced by neither exe (HSPR0-1, MSPR0-1, MUSIC1-*,
   SEL*/SELP*): no HW behaviour attaches to them in HIDDEN.EXE.

## Appendix A. Functions that differ 1996 vs CD (identical in CD and HIDDEN)

From `re_hw/cmp96.py` over the 1453 1996 functions that map to CD (237 more could not be mapped -
mostly tiny stubs, the creature state stubs and the CRT - and were not checked):
ai_mode1_upgrade_castle_12470, ai_mode3_fly_to_castle_site_12560, ai_mode12_idle_12680,
ai_mode11_return_home_126f0, ai_mode6_collect_mana_12830, ai_mode7_attack_castle_12950,
ai_mode8_attack_wizard_12a90, ai_goal_upgrade_castle_12df0, ai_approach_target_140d0,
ai_cast_spell_14240, ai_spell_ready_14640, ai_choose_attack_spell_14c70,
ai_choose_castle_attack_spell_14f00, ai_record_threat_from_projectiles_150f0,
ai_counter_projectile_15460, ai_has_any_attack_spell_154e0, player_function_keys_156b0,
player_local_input_16660, player_queue_command_17270, creature_follow_leader_18e90,
render_frame_1fab0, ui_fill_bar_212f0, ui_draw_player_list_21370, ui_draw_status_bars_219f0,
ui_draw_sprite_blend_224e0, ui_draw_thing_label_22870, game_tick_update_32e80, config_parse_33750,
player_commands_process_3a8b0, level_load_levels_dat_3bfc0, demo_save_state_3c2c0, init_early_3c800,
file_missing_3cc60, cd_check_files_3cf20, level_load_file_3d160, player_apply_controls_40e70,
castle_spill_mana_41720, timer_isr_install_4ac79, ui_draw_debug_overlay_4ad80, tmaps_load_4b580,
music_init_from_sndsetup_4d2b0, sound_init_from_sndsetup_4d8f0, fe_init_state_51ed0,
fe_screen_config_521c0, fe_screen_language_56940, fe_sndsetup_read_57af0,
fe_savegame_read_names_58f60, fe_savegame_load_59030, fe_savegame_save_591d0, cpu_detect_5ac80,
snd_midi_init_driver_5fa77, vga_draw_box_603f0, vga_draw_rect_outline_640_604c0,
gfx_fill_rect_320_60590, gfx_fill_rect_clipped_6acd4, crt_ltoa_wrap_7091a,
crt_heap_alloc_block_70997, crt_heap_free_block_70a4c, poly_fill_triangle_722e3.
(Some of these may be register-allocation-only differences; each needs its own look.)
