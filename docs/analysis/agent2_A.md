# Region A (0x10000-0x1f6b0): AI wizard, Thing helpers, creature state helpers - round 2

Scope: the 112 unnamed functions in docs/analysis/todo2_A.txt plus the 4 UNCERTAIN names. Every body was read
(decompiled C; disassembly checked for every zero-caller function to decide fragment vs. real start).
26 entries are tail fragments of their predecessor (no prologue; the predecessor's decompile already contains the
code) and are marked `FRAGMENT`. Names ending in `?` are low confidence.

Big picture: 0x11820-0x155xx is one source file, the **computer-controlled wizard AI** (class 3 type 1 players,
update handler `player_type1_s1_update_11de0`). 0x18610-0x19a90 are the shared creature state bodies and the
per-creature attack callbacks; 0x1a760-0x1ef80 are small per-creature helpers.

## 1. CSV

```csv
0x00010010,dos_is_cdrom_drive_10010,int 21h AH=19h current drive then AH=36h free space; CD-ROM signature = total clusters 0xFFFF / avail 0 / 1 sector per cluster / 0x800 bytes per sector; caches result in DAT_000938c0; returns 1 if CD (caller 3bbd0)
0x000101b0,spiral_search_init_101b0,"builds the spiral ring tables: file_load_rnc into the screen buffer (32x32 byte image of ring index per cell) then for ring r=0..31 collects (dx,dy,r) 4-byte offsets into DAT_000ade28[r] (12-byte records: ptr + count DAT_000ade2c) and points the 100 iterators at 0xac168 (0x18 stride) at the pool"
0x000107f8,FRAGMENT,tail of thing_find_mana_near_10730 (loop continuation after thing_collide; no prologue)
0x00010870,thing_find_mana_ball_touching_10870,spiral search around thing pos for a collidable (flag 8) effect type 0x27 Mana ball whose bbox overlaps (thing_collide_105c0); returns ball ptr or 0 (caller projectile type 18 update 45c90)
0x000108a1,FRAGMENT,tail of thing_find_mana_ball_touching_10870 (mid-flow; garbage decompile)
0x00010980,thing_find_collision_other_owner_10980,spiral search like thing_find_collision_105f0 but without the flag-8 test and requiring a different owner (+0x18); filter by +0x42 class / +0x43 type (0xff = any); used by effect_mana_ball_update_25980 to find what pushes the ball
0x000109b1,FRAGMENT,tail of thing_find_collision_other_owner_10980
0x00010ac0,thing_exists_near_pos_10ac0,"(pos,class,type): scans the 2x2 cells around pos-0x80 for a thing of that class/type within 3D distance 0x80; returns 1/0 (caller effect type16 update 243b0)"
0x00010b85,FRAGMENT,tail of thing_exists_near_pos_10ac0
0x00010c2e,terrain_max_corner_level_10c2e?,(pos) max of the 4 heightmap bytes DAT_000ddfb0 around the pos cell >> 5; real function start (2-byte pad) but no callers; probably unused variant of terrain_height_at
0x00010c70,FRAGMENT,tail of terrain_max_corner_level_10c2e (second half of the 4-corner max)
0x00010cae,terrain_minmax_along_path_10cae?,"(pos,yaw,pitch,steps,out[3]): out[0]=max out[2]=min (init 0 / 0x40000000) of terrain_height_at along a ray advanced with pos_move_polar for steps+1 samples; returns 1; no callers"
0x000110f6,FRAGMENT,tail of thing_area_damage_10d20 (cell loop)
0x000113f6,FRAGMENT,tail of thing_area_damage_11160 (cell loop; trees class 2 type 0 take amount/10)
0x0001168a,FRAGMENT,tail of thing_area_damage_11450 (cell loop)
0x000116dc,thing_try_damage_116dc,"(attacker,victim,dmgtype,amount): if victim alive+flag 8 and (+0x1c & (1<<dmgtype)) and attacker filter +0x42/+0x43 matches and different owner and bboxes overlap -> write damage slot victim+0x5a+dmgtype*6 {i32 amount,u16 attacker idx}; no callers"
0x000117c0,thing_add_pending_damage_117c0,"(attacker,victim,dmgtype,amount): victim+0x5a+dmgtype*6 = amount (or += when a source is already pending); +0x5e+dmgtype*6 = attacker owner idx; the 0x24 bytes +0x5a..+0x7d are 6 damage-type slots of 6 bytes"
0x00011820,castle_near_thing_11820?,returns 1 if any player type 2 (castle) in list cfg+0x8e6e is within (bbox x/y extents sum + 0x300) of the thing; no callers
0x000118c0,castle_crush_wizards_118c0,thing_set_castle_extents then for every Wizard effect (type 0x2d list cfg+0x8e76) whose x/y lie within the castle half-extents+0x100 set its +0xc = -1 (kill); restore extents (caller castle state 5 update 41500)
0x00011980,castle_footprint_clear_11980,castle site test: set castle extents; fail if any other castle (type 2 player) collides; then scan DAT_000fdfb0 cell flags bit 0x80 (built) over the footprint rectangle (cells of half-extents +0x50/+0x52 around pos) in 4 strips; returns 1 when clear (callers ai_spell_ready_14640 / 14980 / castle update 41500)
0x00011be0,castle_site_clear_at_pos_11be0,(pos): no castle (player type 2) within 0x800+extent on x and y and no DAT_000fdfb0 bit 0x80 cell in the 8x8 cell block starting at cell-8 -> returns 1 (caller 45530 castle-spell projectile)
0x00011f20,player_ai_wizard_tick_11f20,"AI wizard per-tick (called by player_type1_s1_update_11de0): cache human wizard (15540); recover fireball burst counter rec+0x194; decrement 24 spell cooldowns rec+0x2d4[]; drift 8 threat values rec+0x1cc[p] toward 0x601f (up by rec+0x20a+1; down by 0x100-rec+0x20a when grudge rec+0x1ce==0); rebuild spell table (40240); inside own castle (collide with rec+0x32) -> rec+0x14b=2 invulnerable (damage slots zeroed) and fast regen (mana +0x84=+0x88/200 min 1000; health rec+0x155=max/200) else slow (/2000 min 100; /500); death via 40b70==2 -> state 2; move (13b10); mana +0x8c += +0x84 clamped to +0x88; spawn spells (14b00); every 0x40-rec+0x20e/4 ticks dodge incoming projectile (153b0/15420/15460) and heal if hurt (cast spell 14240); clamp z to descriptor clearance +0xa/+0xc"
0x00011f71,FRAGMENT,tail of player_ai_wizard_tick_11f20 (cooldown loop onward)
0x00012039,FRAGMENT,tail of player_ai_wizard_tick_11f20 (threat drift loop onward; starts mid instruction)
0x00012330,ai_choose_goal_12330,"AI goal selector run every tick after the mode handler: build castle (12bd0 -> mode 3); low health retreat (12f70 -> mode 0xb); then only every 0x40-rec+0x20e/4 ticks: upgrade castle (12df0 -> 1); attack enemy castle (13000 -> 7); attack enemy wizard (13210 -> 8); attack type-3 player (13440 -> 9); collect mana ball (12e90 -> 6); hunt creature (13770 -> 0xd); default 13a20 (0xb home / 0xc idle). Mode byte = playerrec+0x19f"
0x00012470,ai_mode1_upgrade_castle_12470,"mode 1 handler (stack arg = spell id): if target +0x92 still valid (signature +0x94) aim yaw +0x22 at it; approach (140d0); when in range cast (14240); on failure stop (rec+0xc=0) and move z toward target z+0x200 by descriptor +0xe"
0x00012560,ai_mode3_fly_to_castle_site_12560,"mode 3: aim at home pos +0x96..+0x9a (chosen castle site); approach; cast castle spell there (14240); z toward site height+0x200"
0x00012600,ai_mode4_approach_target_12600,mode 4: if target valid aim and approach; returns 0 once in range
0x00012680,ai_mode12_idle_12680,"mode 0xc: if spell (stack arg) ready cast it; else if that spell is active (+0x30>0) wait; else cruise: rec+0xc = base speed +0x80, rec+0xe = 1"
0x000126f0,ai_mode11_return_home_126f0,"mode 0xb: if own castle (rec+0x32) exists: when farther than 0x2800 (dist^2 0x6400000) try casting spell else approach the castle; without castle: cast if ready else cruise"
0x00012830,ai_mode6_collect_mana_12830,"mode 6: approach the mana ball target; when in range cast (mana magnet); if cast succeeds and ball is within 0x1c of facing claim it (ball+0x90 = own idx)"
0x00012950,ai_mode7_attack_castle_12950,mode 7: approach target castle; every think tick choose a castle-attack spell (14f00) and cast it; otherwise hover (rec+0xc=0) at target z+0x200
0x00012a90,ai_mode8_attack_wizard_12a90,"modes 8/9/0xd (via stub 12a80): approach target; when not in burst recovery (rec+0x194>=0) choose attack spell (14c70); if ready and cast succeeds against a wizard (type 0/1) clear the grudge rec+0x1ce[target player]; else hover at target height"
0x00012bd0,ai_find_castle_site_12bd0,"goal: no castle yet (rec+0x32==0) and castle spell exists (13ac0) and affordable (14ad0): uses thing slot 0 (state+0x74ab) as scratch pos; tries a 4x4 grid of 0x4000-unit quadrant corners and centres (+0x1f00); accepts the first where the nearest other player (13ec0 filter 0xff) is > 0x3000 away (3e860 = max |dx| |dy|) -> home pos +0x96 = site; returns 1"
0x00012d6e,ai_goal_repair_castle_12d6e?,"goal (no callers): if own castle exists and is damaged (+0xc < +8) target it (+0x92 idx; +0x94 signature 14080); returns 1"
0x00012df0,ai_goal_upgrade_castle_12df0,"goal -> mode 1: own castle exists in state 4 with +0x32 == 0 and castle spell ready+affordable+site clear (14980) -> target = castle"
0x00012e90,ai_goal_collect_mana_12e90,"goal -> mode 6: needs spell A (stack); if spell B missing or own total mana +0x88 <= B's cost: nearest suitable mana ball (13ce0) -> target"
0x00012f70,ai_goal_retreat_12f70,goal -> mode 0xb: health below half max and own castle exists -> target = castle
0x00013000,ai_goal_attack_castle_13000,"goal -> mode 7: needs a spell (154e0) and (own castle or no spell X); among other players' castles (type 2): hostile enough (threat[owner] > 50000 - aggression*health/10/255) and farther than 0x1e00 and not touching; or castle richer than (255-aggression)*0x280 + own mana -> nearest -> target"
0x00013210,ai_goal_attack_wizard_13210,"goal -> mode 8: among other wizards (type 0/1) not mid-cast (14c40): grudge rec+0x1ce[p]==1 -> target immediately; else threat high or (idle and their mana +0x8c > (255-aggr)*0x20 + own) -> nearest -> target"
0x00013440,ai_goal_attack_type3_13440,goal -> mode 9: among player things of type 3 (not self) with high threat and total mana +0x8c > (0x113-aggr)*10 and not touching -> nearest -> target
0x000135fe,ai_goal_join_hunt_135fe?,"goal (no callers; real start with 2-byte pad): for creatures not owned by self and any other AI wizard (type 1 player) within 20 cells whose mode matches -> target that creature; decompile is confused"
0x00013770,ai_goal_hunt_creature_13770,"goal -> mode 0xd: needs a spell (154e0); nearest creature over all 20 lists cfg+0x8e1e.. not owned by self with mana +0x8c > 0 -> target"
0x000138b7,ai_set_mode2_138b7,playerrec+0x19f = 2; returns 1 (padded sibling of the 23-byte mode setters 138a0=1 138e0=3 13920=5 13940=6 13960=7 139a0=9 139c0=8 13a00=0xb)
0x000138f7,ai_set_mode4_138f7,playerrec+0x19f = 4; returns 1
0x000139d7,ai_set_mode10_139d7,playerrec+0x19f = 10; returns 1
0x00013a20,ai_goal_default_13a20,fallback: if damaged and own castle exists -> target castle and mode 0xb (go home) else mode 0xc (idle); returns 1
0x00013ac0,ai_get_spell_thing_13ac0,"(thing,spell): spell thing pointer from playerrec+0x2a4[spell] (24 u16 thing indices; rebuilt each tick by 40240 from rec+0x214[]); 0 if none (15 callers)"
0x00013b10,ai_wizard_move_13b10,"AI flight model: pos scratch DAT_000adfc4; ground clamp by descriptor +0xa/+0xc; pos_follow_ground; 2x pos_move_polar; steering rec+0x10 decays by 4; thing_move_to; speed +0x7e steps by 16 toward rec+0xc; turn rate = angle_diff/((255-rec+0x20e)/16+8) clamped to descriptor +2..+4; yaw +0x1e turns toward +0x22 and snaps when passing it"
0x00013c57,FRAGMENT,tail of ai_wizard_move_13b10 (turn-rate clamp and yaw snap)
0x00013ce0,ai_find_mana_ball_target_13ce0,"over mana balls (cfg+0x8e72): unowned (+0x90 owner not class 3) -> candidate; owned by a hostile player -> candidate; owned by friend: only if no nearer wizard (13fa0) is within 20 cells and no castle/wizard (14010/13ec0) touches it; returns nearest candidate"
0x00013ec0,player_find_nearest_by_type_13ec0,"(thing,type): nearest other player thing of given type (0..3; 0xff = any; 1 and 4..0xfe return 0) from list cfg+0x8e6e by dist^2"
0x00013fa0,player_find_nearest_wizard_excl_13fa0,"(thing,excl): nearest player type 0/1 whose owner is neither thing nor excl"
0x00014010,player_find_nearest_castle_excl_14010,"(thing,excl): nearest player type 2 whose owner is neither thing nor excl"
0x000140a0,ai_target_valid_140a0,target still the same thing: thing_signature_14080(target) == +0x94 (signature = class*0x80 + type + owner idx; stored when a goal picks the target)
0x000140d0,ai_approach_target_140d0,"(thing,target,use3d,nearDist,farDist): dist 3D or XY; beyond farDist with spell ready -> cast (14240) unless spell active (14aa0); beyond nearDist -> cruise (rec+0xc=base speed); within nearDist -> stop and return 1"
0x00014240,ai_cast_spell_14240,"(thing,spell): if ai_spell_ready: clear +0x11 bit0; spell 0/0xf (fireball/lightning) need target within 0xaa of facing; set pitch to target; cooldown rec+0x2d4[spell] = DAT_000938c4[spell]; start cast (spell +0x30 = +0x32); fireball burst rec+0x194 (>7 -> negative recovery scaled by rec+0x20e); 1/4/5/0xe/2/6/9/10/0xc instant; default needs 0xe3 facing; spell 0x10 (Castle): if castle exists cast (upgrade) else thing_create(class 3 type 2) owner=self and rec+0x32 = new castle idx"
0x00014640,ai_spell_ready_14640,"(thing,spell): spell thing exists; cooldown rec+0x2d4[spell]==0; wizard mana +0x8c >= spell cost +0x88; aimed spells (0 0xb 0xd 0xf 3 7 8 0x11) also need target within accuracy ((255-rec+0x20c)/4+0x14)*0x800/360 of facing; 3/7/8/0x11/4/0xc/0xe not already casting; 0x10 castle: with a castle also site clear (11980) and aim"
0x00014980,ai_castle_spell_ready_14980,castle spell variant of 14640 using cooldown rec+0x2f4 and total mana +0x88; with existing castle needs site clear (11980) and facing (caller ai_goal_upgrade_castle)
0x00014aa0,ai_spell_active_14aa0,"(thing,spell<24): spell thing exists and is mid-cast (+0x30 > 0)"
0x00014ad0,ai_can_afford_spell_14ad0,spell cost (+0x88 of spell thing) <= wizard total mana +0x88
0x00014b00,ai_spawn_spells_14b00,"spell acquisition timers: for each of 24 slots with no spell thing (rec+0x2a4[s]==0) count down rec+0x274[s]; at 0 call the spell create handler via DAT_000962b6+s*0xe (class 12 table B handler column) at own pos; new spell thing gets flag 1 and caster +0x2a = self and is stored in the first free rec+0x214[0..23]"
0x00014bf9,FRAGMENT,tail of ai_spawn_spells_14b00
0x00014c40,ai_spell_in_progress_14c40,returns the spell thing if it exists and +0x30 > 0 else 0
0x00014c70,ai_choose_attack_spell_14c70,"wizard attack spell choice: sets conserve flag rec+0x196 when mana < total/4 (clears above /4+6000 or half); if not conserving returns the first ready of 0x11 skeleton / 8 volcano / 7 meteor (skipped with probability rec+0x20c/255) / 0 fireball / 0xf lightning; 0xff = none"
0x00014f00,ai_choose_castle_attack_spell_14f00,same as 14c70 without the random skip (used in castle attack mode 7)
0x000150f0,ai_record_threat_from_projectiles_150f0,"confirmed: per tick for each class-9 projectile not yet scored (+0x11 bit 0x20) fired by a player: target wizard -> threat[shooter] in target's playerrec+0x1cc += 3000 (types 3/4/0xb) or 500; target castle -> += 5000/1000 and set grudge +0x1ce=1 when above the hostility threshold; projectile type 1 hitting a player-owned mana ball -> owner's threat[shooter] += ball mana/4; values clamp 0xffff"
0x000153b0,ai_find_incoming_projectile_153b0,nearest class-9 projectile (cfg+0x8e7a) whose target +0x92 is self; within 20 cells (dist^2 < 0x1900000) else 0
0x00015406,FRAGMENT,tail of ai_find_incoming_projectile_153b0
0x00015460,ai_counter_projectile_15460,"(thing,projectile): if within 0x400 cells^2 (dist^2 <= 0xfffff): projectile type 0/3 -> cast first ready of two spells; type 4/9 -> cast one (shield / rebound reactions); via ai_spell_ready + ai_cast_spell"
0x000154e0,ai_has_any_attack_spell_154e0,returns 1 if any of five spells (stack args) exists in the spell table 13ac0
0x00015540,ai_cache_human_wizard_15540,walks player list cfg+0x8e6e and stores the last type-0 (human) wizard thing in DAT_000acac0 (body continues in the mis-sized FUN_00015564)
0x00017ff0,creature_kill_all_17ff0,cheat: for all 20 creature type lists set health +0xc = -1 (caller player_commands_process_3a8b0)
0x00018610,creature_idle_seek_leader_18610,"(thing,attackState): shared creature state body +0: apply pending damage slot 0 (+0x5a/+0x5e -> +0x28) and min health along segment chain +0x36; dead -> +0x26 killer and set state; hit by a player (class 3) -> target +0x92 = attacker and set state; else every descriptor+0x1a ticks find a same-type creature (list cfg+0x8e1e+type*4) with no leader within sight radius +0x1c and fov +0x1e -> +0x34 = leader and set state (10 callers = state+0 of each creature)"
0x00018c20,creature_attack_target_18c20,"(thing,stateA,stateB,attack_cb): shared state +1: damage/death handling; creature_move_step; every 4 ticks aim +0x22 at target +0x92; target dead or deleted -> set state; every desc+0x1a ticks: beyond sight radius -> set state else call attack_cb(dist) (pushed by caller: 193f0 dragon/worm 1961e vulture/bee 1949e archer/emu 19680 crab 19940 troll 199ee griffon) (9 callers)"
0x00018e90,creature_follow_leader_18e90,"(thing,baseState): shared state +2 flocking: no leader -> debug DAC colour 0 red (4cdf0); damage/death handling; move; every desc+0x1a ticks switch(leader state - base): 2 -> take leader's target and set state; 3 -> adopt leader's leader; 0/1 -> aim at leader (separation: aim away from same-type within 0x100) and speed +0x7e = leader speed + leader+0x82; other -> drop leader; attacked by player -> self and leader target the attacker (13 callers)"
0x000191d0,creature_die_191d0,"(thing,state): shared state +3: set state on every segment (+0x36 chain) propagating killer +0x26; if killer is a human wizard (player type 0) and creature is independent and type not 9/0xc/0xd/0xe/0xf -> killer playerrec+0x167 (kills) ++; set own state (17 callers)"
0x00019310,creature_dead_drop_mana_19310,"shared state +4: every 8 ticks: cheat bit (cfg+1 & 2) sets carried mana 5000; effect_spawn_mana_ball_25fe0 drops +0x8c as a mana ball; thing_create (death effect) owned by self; thing_mark_delete (17 callers)"
0x00019360,creature_adopt_leader_19360,"(thing,other,state): +0x34 = other's leader (if alive and not self) or other itself; +0x46 = state"
0x000193f0,creature_attack_fire_193f0,"attack callback (dragon s2 / worm s20): thing_create class-9 projectile; +0x44=10 +0x45=0; owner; yaw/pitch to target; z += half height +0x54; descriptor DAT_00096ad0; inherits target +0x92 and filters +0x42/+0x43; damage +0x2c = 500"
0x0001949e,creature_attack_arrow_1949e,"attack callback (archer s26 / emu s62; pushed as 0x194a0): projectile with damage 0xfa and doubled bbox (thing_set_sprite_double_35340)"
0x00019550,skeleton_attack_fire_19550,skeleton s56 attack: projectile with damage 400 (600 when the skeleton has a mana owner +0x90) and doubled bbox
0x0001961e,creature_attack_melee_1961e,"attack callback (vulture s8 / bee s14; pushed as 0x19620): if 3D distance to target < 0x400 add pending damage (thing_add_pending_damage_117c0)"
0x00019680,creature_attack_volley_19680,"attack callback (crab s32): n = carried mana*7/total (1..5); random branch: 0 -> n small projectiles (+0x45=0 descriptor DAT_00096a10+(6-i)*0x20 dmg 400); 1-2 -> n projectiles +0x45=0x17 dmg 800; 3-6 -> one big projectile descriptor DAT_00096a70 dmg 8000"
0x00019940,creature_attack_fire_troll_19940,attack callback (troll s44): projectile descriptor DAT_00096ad0 damage 0x30c
0x000199ee,creature_attack_fire_griffon_199ee,attack callback (griffon s50; pushed as 0x199f0): projectile +0x45=0x17 descriptor DAT_00096ad0 damage 4000 filters copied from target
0x00019a4c,FRAGMENT,tail of creature_attack_fire_griffon_199ee
0x00019a90,creature_attack_fire_offset_19a90?,projectile with descriptor DAT_00096a50 spawned at thing pos moved forward by pos_move_polar then thing_move_to; no direct reference found (callback passed in a register/stack)
0x0001a760,creature_set_attack_sprite_1a760,"(thing,sprite): advance RNG +4; speed 0; thing_set_sprite; collision filter +0x42/+0x43 = target's class/type (archer s25/s27)"
0x0001a7f0,creature_set_move_sprite_1a7f0,"(thing,sprite): speed = base +0x80; set sprite; filter class 3 any type (archer s26)"
0x0001aef0,creature_target_nearest_mana_ball_1aef0?,scan whole pool for effect type 0x27 Mana ball nearest in 3D (< 0x10000) -> +0x92 (0 if none); no callers (crab variant of mana seeking)
0x0001b20a,FRAGMENT,inside creature_kraken_s38_update_1b000 (garbage decompile)
0x0001b231,FRAGMENT,"inside kraken s38: burst counter +0x47=5 then one projectile per tick (+0x45=0x17 dmg 800 descriptor DAT_00096ad0) with sound; no prologue"
0x0001b348,FRAGMENT,tail of the kraken projectile spawn in 1b231
0x0001c1e0,skeleton_convert_villager_1c1e0?,"(skeleton s55 helper): damage/death handling; +0x1a<0 counts up then 1c8c0 resets (+0x1a=400 sprite); hit -> +0x1a=-50; every desc+0x1a ticks cycle through creature lists type 4 Archer (cfg+0x8e2e) / 12 Builder (0x8e4e) / 13 Townie (0x8e52) within sight radius; if the victim is within 0x600 (3D) delete it and thing_create at its position owned by self (raise a new skeleton?)"
0x0001c7e0,skeleton_set_attack_sprite_1c7e0,if target has the same owner -> set state (stop) else speed 0 + sprite + filter = target class/type (skeleton s55/s57)
0x0001c860,skeleton_set_idle_1c860,speed = base; sprite; filter class 3 any; +0x1a = 0x32; +0x47 = 0 (skeleton s54/s56)
0x0001d220,creature_reset_target_1d220,+0x92 = 0; +0x1a = 0; set state; sound_request (genie s68)
0x0001d270,genie_teleport_to_target_1d270,if target: +0x1a=0; set state; move to target pos + polar offset via thing_move_to (genie s67)
0x0001d310,genie_absorb_mana_ball_1d310,"if carried +0x8c < total +0x88: nearest Mana ball (type 0x27) within sight radius -> +0x8c += ball mana; ball owner 0; delete ball; thing_create effect at ball pos owned by self with flag 0x100; sound (genie s67/s68)"
0x0001d420,terrain_rect_is_flat_1d420,"(x,y,w,h,maxDiff): terrain_height_range_34820 (max-min of the 4 corner heights) < maxDiff"
0x0001d4b0,castle_size_half_extents_1d4b0,"(size,&hy,&hx): from the 6-byte castle size table (+4 w,+5 h; halved in 320x200 mode DAT_0012edae==1): extent = dim*0x80 + 0x300"
0x0001d540,creature_genie_s72_place_castle_1d540,"confirmed: Genie state 72: target must be a Wizard effect (type 0x2d) else state back; try up to 4 placements (+0x1a 1..3 = offsets east/west/south of the wizard by castle half-extents + random cells); reject water (terrain type 1); require flat terrain (1d420) and no wizard effect or castle overlapping; then thing_create + effect_wizard_init_35090 and set the new thing to state 0x33 (castle build); clear target and change state"
0x0001dae5,FRAGMENT,inside creature_builder_s74_update_1dc20 region (kill-credit code: killer owner type 0/1 -> playerrec+0x210 = 200)
0x0001dd30,FRAGMENT,inside builder s74 (kill credit)
0x0001dd5c,FRAGMENT,inside builder s74
0x0001dfa7,FRAGMENT,inside creature_builder_s75_update_1de90 (kill credit)
0x0001e26c,FRAGMENT,inside creature_townie_s79_update_1e140 (kill credit)
0x0001e3f6,FRAGMENT,inside townie s79 (target idx = (ptr-base)/0xa4; speed = base)
0x0001e48a,FRAGMENT,inside creature_townie_s82_update_1e500
0x0001e6ec,FRAGMENT,inside creature_trader_s85_update_1e5c0 (kill credit)
0x0001e864,FRAGMENT,inside trader s85 (target idx; speed = base)
0x0001ef10,creature_stop_set_sprite_1ef10,advance RNG; speed 0; thing_set_sprite (type15 s91)
0x0001ef80,creature_type15_wander_1ef80,"type15 ground wander (caller s91 1ea60): every 8 ticks check cell type mask vs descriptor +0x14 (forbidden -> state 0x5e=94); score 4 headings (+0x200 steps; weights from the 4 u16 at 0x1ea40) with creature_check_terrain and pick the best yaw; every 16 ticks snap the lateral axis to the cell centre (+0x80) by heading quadrant (walks along cell lines); separation from same-type within 0x100; 50% chance to skip turning; pos_move_polar + ground follow + thing_move_to"
0x0001ac80,creature_crab_s33_collect_mana_1ac80,"confirmed: Crab state 33: damage/death handling; move; every +0x1a ticks: target must be a Mana ball else state back; within 5*base speed -> absorb (+0x8c += ball +0x8c; ball owner 0; delete; set state; crab_update_sprite_by_mana_36ac0 which also adds 5000 max health per level); within 20*base speed -> +0x1a=3 (think faster); aim at ball"
0x0001c570,creature_skeleton_s56_attack_1c570,"confirmed: Skeleton state 56: damage/death handling; move; aim every 10 ticks; every desc+0x1a ticks: within sight radius (+ castle half-extent when target is a castle) -> skeleton_attack_fire_19550 else set state; if state changed -> skeleton_set_idle_1c860"
```

## 2. Notes for docs/ENGINE.md

### 2.1 Computer-controlled wizard AI (class 3 type 1; 0x11820-0x155xx)

Player type 1 is the AI opponent wizard. Its Table A state-1 handler `player_type1_s1_update_11de0` calls
`player_ai_wizard_tick_11f20` (housekeeping + movement) then dispatches on the **AI mode byte playerrec+0x19f**
and finally runs `ai_choose_goal_12330`, which may switch mode through 23-byte setters (0x138a0..0x13a00).

| mode | handler | meaning |
|---|---|---|
| 0 | - | choose a goal |
| 1 | ai_mode1_upgrade_castle_12470 | fly to own castle and cast the castle spell (upgrade) |
| 2, 5 | FUN_00012550 (ret 0) | unused |
| 3 | ai_mode3_fly_to_castle_site_12560 | fly to home pos +0x96 and cast the castle spell |
| 4 | ai_mode4_approach_target_12600 | approach target |
| 6 | ai_mode6_collect_mana_12830 | collect a mana ball (claim ball +0x90 when facing it) |
| 7 | ai_mode7_attack_castle_12950 | attack an enemy castle (spell from 14f00) |
| 8, 9, 0xd | ai_mode8_attack_wizard_12a90 | attack wizard / type-3 player / creature (spell from 14c70) |
| 0xb | ai_mode11_return_home_126f0 | return to own castle |
| 0xc | ai_mode12_idle_12680 | idle cruise |

Goal priority (12330): castle site (12bd0) > retreat when health < half (12f70) > [every think tick] upgrade
castle (12df0) > attack castle (13000) > attack wizard (13210) > attack type-3 (13440) > collect mana (12e90) >
hunt creature (13770) > default (13a20). Think tick = `counter % (0x40 - rec+0x20e/4) == 0`.

Spell ids used by the AI (confirming the create-table order): 0 fireball, 7 meteor, 8 volcano, 0xf lightning,
**0x10 = Castle** (missing from the round-1 list; `ai_cast_spell_14240` case 0x10 creates class 3 type 2),
0x11 skeleton. `DAT_000938c4` = 24 x u16 spell cooldown reload values. `DAT_000962b6 + spell*0xe` is the
handler column of the class-12 Table B (so Table B for spells starts at 0x962ae).

AI-specific player-record fields (0x801-byte records; pointer at thing+0xa0):

| off | meaning | evidence |
|---|---|---|
| +0xc | desired speed (0 or thing+0x80 base) | 13b10, 140d0 |
| +0xe | "moving" flag | 12680 |
| +0x10 | steering value, decays by 4/tick; 0x50 when dodging a projectile | 13b10, 15420 |
| +0x14b | inside-own-castle timer (2): damage slots cleared, fast regen | 11f20 |
| +0x146 | u8 counter to 200 | 11f20 |
| +0x155 | health regen per tick (max/200 in castle, max/500 outside) | 11f20 |
| +0x15f | countdown | 11f20 |
| +0x167 | kills of independent creatures by this wizard | 191d0 |
| +0x194 | fireball burst counter; negative = recovering | 14240, 11f20 |
| +0x196 | conserve-mana flag | 14c70 |
| +0x19f | AI mode | 11de0 |
| +0x1cc[8] | 8-byte entries {u16 threat toward player p (drifts to 0x601f), u16 grudge flag} | 11f20, 150f0, 13000 |
| +0x20a | aggression 0..255 (threat drift rate and hostility threshold `50000 - aggr*health/10/255`) | 11f20, 13000 |
| +0x20c | accuracy 0..255 (aim tolerance ((255-acc)/4+20) degrees; random skip of big spells) | 14640, 14c70 |
| +0x20e | reaction 0..255 (think period 0x40-r/4; turn rate) | 12330, 13b10 |
| +0x210 | set to 200 when one of this player's creatures kills a builder/townie/trader (fragments 1dae5..1e6ec) | 1e0b0 |
| +0x214[24] | spell thing indices (slots) | 14b00, 40240 |
| +0x274[24] | spell acquisition countdowns | 14b00 |
| +0x2a4[24] | spell thing index by spell id (rebuilt each tick by FUN_00040240) | 13ac0 |
| +0x2d4[24] | spell cooldowns (0x2f4 = castle spell) | 14640, 14980 |

`ai_record_threat_from_projectiles_150f0` (was hit_record_scores) is the only writer of +0x1cc from combat; it runs
from thing_update_all for all players, so human players also accumulate threat tables (unused for them).

### 2.2 Damage slots

thing+0x5a..+0x7d is **six 6-byte damage slots indexed by damage type** `{i32 amount, u16 attacker idx}`
(`thing_add_pending_damage_117c0`, `thing_try_damage_116dc`, memset of 0x24 bytes in 11f20). The creature code
only consumes slot 0 (+0x5a/+0x5e); thing+0x1c is the bitmask of damage types a thing accepts (`1 << type`).
This supersedes the round-1 reading of +0x60/+0x64/+0x76 as separate fields for mana balls (they are slots 1 and 4).

### 2.3 Creature state machine layout (confirmed for all 17 types)

Each creature type occupies 6 consecutive Table A states: +0 idle / seek flock leader
(`creature_idle_seek_leader_18610`), +1 attack (`creature_attack_target_18c20` with a per-type attack callback
pushed on the stack: dragon/worm 193f0, vulture/bee melee 1961e, archer/emu arrow 1949e, crab volley 19680,
troll 19940, griffon 199ee), +2 follow leader (`creature_follow_leader_18e90`), +3 dying (`creature_die_191d0`,
kill credit playerrec+0x167), +4 dead (`creature_dead_drop_mana_19310`: mana ball + death effect + delete),
+5 type-specific (e.g. dragon s6 = seek + move; crab s33 = collect mana; skeleton s56 = attack with 19550;
genie s72 = place a castle for its Wizard target). Flocking: +0x34 leader, speed copied from the leader plus
leader+0x82; separation radius 0x100.

Descriptor (+0x9c) fields used here: +2/+4 min/max turn rate, +0xa/+0xc ground clearance, +0xe z step,
+0x14 allowed-terrain mask, +0x1a think period, +0x1c sight radius, +0x1e fov. Attack projectiles use
descriptors DAT_00096a10 (+i*0x20), 96a50, 96a70, 96ad0.

### 2.4 Castle placement helpers

`castle_footprint_clear_11980` / `castle_site_clear_at_pos_11be0` test DAT_000fdfb0 bit 0x80 (built cell) over
the castle rectangle and castle-vs-castle overlap; `castle_crush_wizards_118c0` kills Wizard effects under a castle
footprint; `castle_size_half_extents_1d4b0` reads the 6-byte castle size table (+4 w, +5 h). The Genie creates
castles for Wizard effects (type 0x2d) via `effect_wizard_init_35090` + state 0x33.

### 2.5 Misc

- `spiral_search_init_101b0` builds the ring tables from a 32x32 image loaded with file_load_rnc (the ring map
  file; name not recoverable here) - DAT_000ade28 = 32 x {ptr, count} 12-byte records.
- `dos_is_cdrom_drive_10010`: CD-ROM detection by int 21h/36h signature (0xFFFF clusters, 2048-byte sectors).
- Thing slot 0 (state+0x7463) is used as a scratch Thing by the AI castle-site search (12bd0 writes +0x18/+0x48).
- `FUN_0004cdf0` writes a VGA DAC entry (out 3c8h/3c9h); `creature_follow_leader_18e90` flashes colour 0 red when a
  follower has no leader - a leftover debug aid.

### 2.6 Open questions

- Which file the ring image in 101b0 comes from (file_load_rnc arguments are in registers).
- Player type 3 (goal 13440) and creature type 15 (wander along cell lines, 1ef80): roles still unnamed.
- `skeleton_convert_villager_1c1e0` creates a thing at the victim position with class/type in registers; verify it
  is a skeleton (would explain why skeletons hunt archers/builders/townies).
- `creature_attack_fire_offset_19a90` is unreferenced in the call graph; the kraken (1b000) and wyvern handlers may
  pass it in a register.
- The mode-2/5 stub FUN_00012550 and goals 12d6e / 135fe have no callers: cut features of the AI.
