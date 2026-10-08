# Local input port (input.cpp) - agent F report, port round 3, 2026-10-06

Files: `src/mcengine/input.h` / `input.cpp`, `tables/input.tables` -> `gen/input_tables.h`,
`src/tests/input_test.cpp` + `input_test.cmake`. Build dir `build_F`; `input_test.exe` -> exit 0, zero
warnings (/W4). No other file was touched.

## Functions translated

| port | original |
|---|---|
| `player_local_input()` | player_local_input_16660 (jump table 0x1661c: mode 0 / 4 flight, 1, 2 spell book, 3 chat line) |
| `player_function_keys()` | player_function_keys_156b0 (jump table 0x15688: F1..F10) |
| `player_mouse_steer()` | player_mouse_steer_15590 |
| `player_queue_command(cmd, arg)` | player_queue_command_17270 (2 stack args: cmd, arg; jump table 0x171f0, 31 entries) |
| `creature_kill_all()` | creature_kill_all_17ff0 (no args, result unused by its caller at 0x3b969) |
| `input_key_raw(byte)`, `input_key_event(scancode, down)` | input_keyboard_isr_4fa28 (state side; port I/O, EOI and the Tab debug chain are not) |
| `input_mouse_event(mask, x, y)`, `input_mouse_move`, `input_mouse_button` | mouse_event_callback_5b86c (state side; the pointer redraw is not) |
| `input_reset()` | input_keyboard_install_4fb46 (table clear) + start values of the mouse variables (0x9e5d8..) |

`input_register_handlers()` installs `g_hook_player_local_input = player_local_input`.

Extra functions outside 0x15590..0x17a80 (small, nobody's task, needed to make the input usable):

| port | original |
|---|---|
| `input_mouse_set_pos(x, y)`, `input_mouse_center()` | input_mouse_set_pos_4a030, input_mouse_center_4a000 (int 33h call -> `g_hook_input_mouse_warp`) |
| `input_snapshot()`, `input_changed()` | input_snapshot_34060, input_changed_34090 (the movie player's "user touched something" latch) |
| `input_book_update_selection()`, `input_book_cell_origin()` | the hit test inside render_frame_1fab0, 0x20b44..0x20d90 (input mode 2): the only writer of Config+0x16, which the book input reads. HUD code; translated without the drawing. |
| `input_text_load(dir, language)`, `input_text(i)` | the language switch of fe_screen_language_56940 + text_split_lines_3ed30(text, 0xadce8, 0x50): the 80 strings of data/{e,f,g,i}text.dat the function keys copy their notices from |

Not translated: `ui_button_table_dispatch_17a80` (**no caller anywhere**: dead code; record layout in
"Corrections"), `cue_script_step_17d80` (intro cue interpreter, sits in the address range but is not
input), `input_joystick_poll_5a4e0`.

## Device state (input.h)

Plain globals under the original meaning; the platform never writes them directly.

| port | original | maintained by | read / cleared by |
|---|---|---|---|
| `g_key_down[128]` | 0x12ee20 | keyboard handler: `[byte & 0x7f] = byte <= 0x7f` | all modes (Ctrl 0x1d, Alt 0x38, Shift 0x2a / 0x36, arrows 0x48 / 0x50 / 0x4b / 0x4d, F1 0x3b) |
| `g_key_last` | DAT_0012eea0 | handler: the raw byte (make, break = make + 0x80, 0xe0, or 0x80 for a swallowed fake shift) | compared with make codes; the consumer writes 0 |
| `g_key_prev_raw`, `g_key_first` | DAT_0012e396 / e397 | handler (0xe0 detection; first make code) | - |
| `g_mouse_x`, `g_mouse_y` | DAT_0009e5dc / 5de | mouse handler, clamped to x <= 0x27e, y <= 0x1de | steering, book hit test |
| `g_mouse_click_x/y` | DAT_0009e5d8 / 5da | handler, position of a click event | (HUD / front end) |
| `g_mouse_click_left / right / middle` | DAT_0012ee0e / 0c / 0a | handler: set on a press when the button was up **and** no click is pending | consumer clears |
| `g_mouse_held_left / right / middle` | DAT_0012ee14 / 12 / 10 | handler | fire-while-held test |
| `g_mouse_dbl_timer`, `g_mouse_dbl_click` | DAT_0012ee06 / 08 | handler (window DAT_0009e5e0 = 0x19); nothing decrements or reads them: dead feature | - |
| `g_mouse_event_mask`, `g_mouse_moved`, `g_mouse_present` | DAT_0012ede0, DAT_0009e5e3, DAT_0009e5e4 | handler / mouse init | - |
| `g_video_mode_flags` (mc_globals) | DAT_0012edae | config, R key | steering divisor, book origin, pointer centre |
| `g_sound_available / on`, `g_music_available / on` | DAT_0009e320 / 321 / 30c / 30d | F1, F2 (platform sets "available") | P, F1, F2 |
| `g_sky_available` | DAT_000adf48 != 0 | port: 1 | F6 gate |
| `g_frame2` (mc_globals) | DAT_000adf70 != 0 | renderer | F9 / F10 gate |
| `g_book_cell_w / h`, `input_book_cell_w/h()` | [DAT_000adf94] + 0x16 / 0x17 | HUD table load | book hit test, joystick rectangle |

"Shift state DAT_0012ed35 bit 0x20" (briefing / ENGINE.md) belongs to the front end's text field
(ui_text_edit_field_58290); the in-game code only tests the two key-table entries.

Keyboard handler facts the feeding API reproduces: an extended key arrives as `e0 xx`; the prefix is
stored like a break code of entry 0x60, so right Ctrl / right Alt / the grey arrows / keypad Enter land
on the same entries as their plain twins; the fake shifts `e0 2a` / `e0 aa` are swallowed (last key
becomes 0x80, Shift is not pressed). A key release overwrites `g_key_last` with the break code.

## (a) Key and mouse bindings of the original, as found in the code

Order of evaluation matters and is given per mode. `gate` = Config.flags & 0x8000 (set by sending the
chat line "RATTY"); `single` = not when Config.flags & 0x10 (network / custom game). All key presses
are read from `g_key_last` (one pending key), held keys from `g_key_down`.

Before anything: if the local player is computer controlled (PlayerRec+9 == 1): **F1 held -> cmd 2
(quit)**, nothing else.

### Function-key layer, `player_function_keys` (called in modes 0, 1, 2; skipped while a command is pending in the packet)

| scancode | key | condition | effect |
|---|---|---|---|
| 0x3b..0x41 | Alt+F1..F7 | name "chronicle" or gate (checked in queue) | cmd 0x1e arg 1..7: all spells / more mana / destroy other wizards / castles / balloons / heal / kill all creatures |
| 0x2f | Alt+V | gate | cmd 4 arg 8 (PlayerRec.flags ^= 8: debug overlay) |
| other | Alt+key | | nothing here, key stays pending (flight layer below) |
| 0x10 | Shift+Q | | cmd 2: quit |
| 0x12 | Shift+E | gate | cmd 0x1a: everybody quits |
| 0x13 | Shift+R | single | restart level: status \|= 0xc, own castle index (P+0x32) = 0 |
| 0x21 | Shift+F | gate | status \|= 4 (level lost) |
| 0x25 | Shift+K | single | own wizard health = -1 |
| 0x26 | Shift+L | single | own castle health = -1 |
| 0x2e | Shift+C | gate | status \|= 2 (level won) |
| 0x19 | P | no Alt / Shift, single | Config.paused ^= 1; pausing stops all sounds / the music (if on), unpausing restarts the level track (i16 GameState+0x240) |
| 0x13 | R | no Alt / Shift; opt_allowed[10], mode_3d == 0 | video_toggle_resolution_33600 (320x200 <-> 640x480) |
| 0x39 | Space | no Alt / Shift | cmd 0xf (respawn; only queued while dead in state 3), then cmd 0x1b (leave; only with status & 2) |
| 0x3b | F1 | sound available | sound effects on / off, notice "Sound On / Off", stops all sounds |
| 0x3c | F2 | music available | music on / off (plays the level track / stops), notice |
| 0x3d | F3 | single | Config.substeps = (n + 1) % 3, notice "speed normal. / fast. / super fast." |
| 0x3e | F4 | opt_allowed[8] | GameState+0x219d ^= 1, "Soften On / Off" |
| 0x3f | F5 | opt_allowed[0] | +0x2195 ^= 1, "Reflections On / Off" |
| 0x40 | F6 | opt_allowed[2], sky loaded | +0x2197 ^= 1, "Sky On / Off" |
| 0x41 | F7 | opt_allowed[1] | +0x2196 ^= 1, "Shadows On / Off" |
| 0x42 | F8 | - | +0x2199 ^= 1 and +0x219a ^= 1, "Icons and Map On / Off" |
| 0x43 | F9 | opt_allowed[7] (0 -> 1 needs the second screen buffer) | +0x219c 0 -> 1 -> 2 -> 0, "Light Speed Blur On" / "Heavy Speed Blur On" / "Speed Blur Off" |
| 0x44 | F10 | input mode 0, opt_allowed[6] (0 -> 1 needs the second buffer) | +0x219b 0 -> 1 (render_mapmode_palette_2ff50, view size = 0x28) -> 2 (video_restore_mode_2ff10) -> 0; no notice |

Every notice is copied to `players[local].messages[local]` with ticks 0x32, arg 2. Alt / Shift
combinations that are not listed return without consuming the key.

### Mode 0 (and 4): flying. Skipped (and both click events dropped) while a command is pending.

After the function keys and the joystick poll, CmdPacket.bits is zeroed, then exactly one of:

| scancode | key | condition | effect |
|---|---|---|---|
| 0x02..0x0b | Ctrl+1..0 | Ctrl held | cmd 0x19 arg 0..9: quick-select key -> right hand. With Ctrl held nothing below runs (no arrows, no fire); the mouse still steers |
| 0x13 | Alt+R | Alt held | cmd 0xc: start recording a movie |
| 0x1f | Alt+S | | cmd 0xa: quick save (movie/gam10000.dat) |
| 0x21 | Alt+F | | cmd 4 arg 0x20 (flag bit nothing reads) |
| 0x23 | Alt+H | | GameState+0x219e ^= 1 (interlaced stereo, VFX1 headset) |
| 0x32 | Alt+M | | cmd 4 arg 0x10 (flag bit nothing reads) |
| 0x2f, 0x3b..0x41 | Alt+V, Alt+F1..F7 | | same commands as the function-key layer (which already consumed them: unreachable) |
| 0x10 / 0x12 / 0x2e | Shift+Q / E / C | Shift held | cmd 2 / 0x1a (gate) / 0x1b (gate); unreachable: the function-key layer consumed these keys first (there C sets the "won" status bit instead of queueing 0x1b) |
| 0x1a | [ | no modifier | view size + 1 up to 0x28 (not in a 3D mode) |
| 0x1b | ] | | view size - 1 down to 0x11 (not in a 3D mode) |
| 0x17 | I | | cmd 0x10: open the chat line (mode 3); the tick ends here (no steering) |
| 0x01 | Esc | | cmd 0x1b (status & 2: leave the won level), else cmd 0x1d (leave the game); tick ends here |
| 0x13 / 0x39 | R / Space | | second copies of the function-key layer's handling (Space would add cmd 0x1c "leave lost level"); unreachable because that layer cleared the key |
| - | left + right click in the same tick | not movie mode (Config.flags & 0x200), alive | cmd 0x14 arg 2: open the spell book |
| 0x02..0x0b | 1..0 | | cmd 0x18 arg 0..9: quick-select key -> left hand |
| 0x1c | Enter | alive | cmd 0x14 arg 2: open the spell book |
| 0x48 / 0x50 / 0x4b / 0x4d | Up / Down / Left / Right (held) | | cmd 6 with bits 1 faster / 2 slower / 4 slide left / 8 slide right |
| - | left button | hand slot != -1 | cmd 6 bit 0x10 on a click event; while held without a new click: only if the hand's spell has `cast_ticks > 0`, or `unk3e != 0 && burst > 0`; a spell with `spell_flags == 1` fires on click events only |
| - | right button | | the same with bit 0x20 |
| - | mouse position | Config.substeps == 0 | steer_x = (x - 320) * 128 / 320, steer_y = -(y - 200) * 128 / 200 (320x200 mode) or -(y - 240) * 128 / 240, truncated, clamped to +-127 |

Consequences of `player_queue_command` (first command of a tick wins): a packet that carries any command
other than 6 has bits 0, i.e. the arrow keys and fire buttons are ignored in the tick the book is
opened or a spell is quick-selected. Holding Ctrl, Alt or Shift also suppresses them. In the fast
modes (F3) the mouse does not steer at all.

### Mode 1 (set by cmd 0x14 arg 1; no queuer of that found in this range)

Function keys; **Enter or both buttons -> cmd 0x14 arg 0** (back to flight).

### Mode 2: spell book

Joystick poll in cursor mode (rectangle 0x280 - 4w, 0xa2, 0x280 - w, 0xa2 + 5h, steps w, h), function
keys, then:

| scancode | key | condition | effect |
|---|---|---|---|
| - | - | wizard dead | cmd 0x14 arg 0 (book closes) |
| 0x02..0x0b | 1..0 | a spell under the pointer (Config+0x16 != 0xff) | packet written directly: cmd 0x17, arg key 0..9, +2 = book slot of that spell (0xff when not found): bind the spell to the quick-select key |
| 0x1c | Enter, or both buttons | | cmd 0x14 arg 0: close |
| - | left click | a spell under the pointer | cmd 0x15, arg = book slot: spell into the left hand, book closes |
| - | right click | a spell under the pointer | cmd 0x16: right hand |

No steering and no key bits in this mode; the key is always consumed. The cell under the pointer comes
from the HUD (hit test below). Book slot = first i with `P.spell_slot[i] == P.spell_thing[order[cell]]`
(`order` = DAT_00097660).

Hit test (HUD, 0x20b44): 24 cells, 4 columns x 6 rows, x from 0x180 in steps of 0x40, y from 0xa2
(DAT_0012edae == 1) or 0xc2, row height = height of HUD sprite 3: 37 (hspr0-0.tab, 640x480) or 36
(mspr0-0.tab 18, doubled by the lo-res tab relocation 0x62a80). A cell is selectable when the player
owns the spell (`P.spell_thing[id] > 0`) and the spell's Thing+0x84 is 0 or `<=` the mana of the player's
castle.

### Mode 3: chat line

Only `g_key_last` 1..0x7f is looked at: **Enter -> cmd 0x13** (send); otherwise the character
`DAT_0009e5e8[scancode]` if it is A-Z, a-z, 0-9, 8 (Backspace) or space -> **cmd 0x11 arg char** (the
table only has upper-case letters). Function keys do not work here. Esc does nothing (its table entry
is 0x27, a decimal 27 typed as hex), so a chat line can only be ended by sending it; nothing in this
range queues cmd 0x12 (cancel) or 0xb (quick load), 0xe (stop recording), 7 / 8 (camera).

`g_input_bindings[]` in input.h lists the same bindings as data (scancode, modifier, context flags,
command, key name, effect text) for the controls help; `MC_SC_*` are the set-1 scancodes.

## (b) What the platform layer calls, and when

Once: `input_reset()`, `input_register_handlers()` (or let `sim_init` / `engine_init` do it, see
requests), `input_text_load(game_dir, g_cfg->language)`; set `g_sound_available` / `g_music_available`
when there is audio; optionally `g_hook_input_platform` (resolution switch, sound stop, music start /
stop, 3D-mode palette requests), `g_hook_input_mouse_warp`, `g_hook_input_joystick_poll`.

Every frame, in this order:

1. Translate every pending OS event:
   - key: `input_key_event(MC_SC_xxx, down)` with the set-1 make code (add `MC_SC_EXT` for extended keys
     if wanted; it changes nothing the game can see). **Forward auto-repeat as repeated down events**: `[`
     `]` and the chat line rely on typematic repeat, as on DOS.
   - mouse motion: `input_mouse_move(x, y)` with the pointer scaled to the virtual screen: 640 x 400 when
     `g_video_mode_flags == 1`, else 640 x 480 (the handler clamps to 638 x 478).
   - mouse buttons: `input_mouse_button(0 left / 1 right / 2 middle, down)`.
2. If `g_state->players[local].input_mode == 2`: `input_book_update_selection()` (the original computes
   it while drawing the book in the previous frame; drop this call once the HUD is ported and writes
   Config.spell_slot itself).
3. `game_tick_sim()` - it calls `player_local_input()` through the hook first (not during movie
   playback), then `player_commands_process()` executes the packet in the same tick.

Nothing has to be cleared afterwards: the input code clears the events it consumed. Things to know:
- There is one pending key. A press whose release arrives before the next tick is lost (the break code
  overwrites `g_key_last`), exactly as in the original at low frame rates. A platform that wants to be
  kinder delivers a release that follows a press in the same frame after step 3.
- "Both buttons" (spell book) needs both click events pending in one tick.
- When the book closes the game centres the pointer (`input_mouse_center`, see requests): the platform
  should warp the OS pointer in `g_hook_input_mouse_warp`, otherwise the wizard turns toward wherever
  the pointer was left on the book. With relative-mouse input, keep a virtual pointer and reset it there.
- For the movie: `g_hook_demo_input_changed = input_changed`, and `input_snapshot()` when playback starts.

## Verification (input_test, numbers from the run)

**Movie check** (`movie/mvi00000.dat`, 34,201 packets, 8551 of the human player): the test loads the
snapshot, installs a hook in place of `player_local_input`, and for every tick builds a device state
for the recorded packet through the feeding API only (mouse position for the steer bytes, arrow key
press / release events for bits 1..8, a fresh button click for bits 0x10 / 0x20, Enter or both buttons
alternately for cmd 0x14, pointer on the book cell + `input_book_update_selection()` + a left / right
click for cmd 0x15 / 0x16, Shift+Q for the final cmd 2, nothing but hovering during book ticks), runs
the real `player_local_input()`, compares all 10 bytes, then lets the recorded packets drive
`game_tick_sim()` so input mode, hand slots and the spell book evolve as in the session:

| recorded cmd | reproduced |
|---|---|
| 0x00 (idle / steering only) | 3766 of 3766 |
| 0x06 (keys / fire) | 4676 of 4676 |
| 0x14 arg 2 (open book) | 54 of 54 |
| 0x15 (left hand) | 45 of 45 |
| 0x16 (right hand) | 9 of 9 |
| 0x02 (quit) | 1 of 1 |
| **total** | **8551 of 8551**, 0 unexplained |

7529 ticks in flight mode, 1022 in the book. This also confirms the structural predictions of the
translation: all 1022 book ticks have zero steering (no mouse_steer in mode 2), the 0x15 / 0x16
packets carry no steering, the 0x14 and quit packets carry steering but no key bits (first command
wins), no packet has both fire bits (two fresh clicks open the book instead), 54 opens = 45 + 9 picks.
The packet waiting in the snapshot's command slot equals packet 0 of the recording.

**Steering formula, statistically**: a steer value that N mouse pixels map onto must be N times as
frequent. Counting the port's own pre-images per value against the recording (values 1..120, both signs):
- x: values with 3 pixels are hit 32.6 times each, values with 2 pixels 21.9 times: ratio **1.49**
  (formula predicts 1.5).
- y with the 640x400 formula: 35.9 vs 18.1, ratio **1.98** (predicts 2.0); with the 640x480 formula
  the grouping gives 0.95, i.e. no fit. 15 packets have steer_y = -127, which the 640x480 path cannot
  produce at all (pointer y <= 478 gives -126). Per eighth of the recording (smaller samples) the
  640x400 grouping gives 1.45..2.24 and the 640x480 grouping 0.58..1.36 (scratch script, not in the
  test). **So the recording was made with DAT_0012edae == 1** throughout (see "Findings").
- After 51 of the 54 book closings the next packet has steer 0, 0 (the other three are 3..27 off):
  the pointer is centred when the book closes, as input_mouse_center_4a000 called from
  player_set_input_mode_3bb50 does.

**By construction** (hand-computed from the disassembly): keyboard handler (make / break, break code as
last key, first key, 0xe0 prefix, fake-shift swallowing, extended twin keys), mouse handler (clamp,
click latch only when the button was up and no click is pending, click position, held flags, double
click variables, no driver), `input_mouse_move` / `set_pos` / `center`, `input_snapshot` / `changed`;
`player_queue_command`: all 31 table entries (unconditional 0 / 1 / 2 / 0x1a, or-ing 4, bit
accumulation 5 / 6, replace 0x11 / 0x14..0x19, never-queued 3 / 7 / 8 / 9 / 0xd, 0xf only when dead in
state 3, 0x1b / 0x1c by status bit, 0x1e by the exact name "chronicle" or the gate); 17 steering cases
in both modes including the clamp ends and the substeps gate.

**Scripted session** on a generated level 38 through `game_tick_sim()` with the hook installed (device
events only through the feeding API): pending join command blocks input and drops clicks; arrows -> bits ->
`P.input_bits`, target speed 0x50, strafe, yaw follows the mouse; a click with an empty hand still
sends the fire bit; Alt+F1 with the configured name "chronicle" -> cheat 1 gives 24 spells; 1..0 and
Ctrl+1..0 select through the quick-select table; click / held / casting / burst / `spell_flags == 1`
fire rules; Enter opens the book, all 24 cells hit-tested (both corners, both screen modes, outside),
digit binds the hovered spell (cmd 0x17 with the book slot), right / left click put the hovered spell
in the hand and close the book, both buttons open it; chat line typed as R A X Backspace T T Y -> text
"RATTY" -> gate opens; F3 cycle with notices and no steering in fast modes, F4..F9 toggles with their
notice texts and "allowed" gates, F9 / F10 second-buffer gate and state cycles, `[` `]` limits, Alt+H,
Alt+V / Alt+M / Alt+S packets, F1 / F2 with and without audio, R request, P pause requests;
`creature_kill_all`: 221 creatures on the lists, 0 alive afterwards, players untouched; Shift+K kills
the wizard, Enter refused while dead, Space respawns (status |= 0xc without a castle), Shift+C + Esc ->
status 10, Esc -> status 8, Shift+R, Shift+Q -> quit flag; computer-controlled local player (F1),
mode 1, unknown mode.

### Verified versus only translated

- Verified against original data: packet layout and semantics of cmd 0 / 2 / 6 / 0x14 / 0x15 / 0x16
  in flight and book mode, the bits, the steering formula incl. clamp and screen-mode branch, the
  first-command-wins rule, pointer centring on book close.
- Verified by construction only (no original data exists for them): everything else in the queue table,
  the keyboard and mouse handlers.
- Only translated (exercised by the session test, but the expectation is my reading of the
  disassembly): function keys and their notices, every Ctrl / Alt / Shift combination, cmd 0x17 / 0x18 /
  0x19 / 0x10 / 0x11 / 0x13 paths, modes 1 and 3, the computer-player path, the held-button fire
  conditions (the recording has runs of the left fire bit up to 76 ticks long, which fits "held while
  casting", but the reconstruction used a click per tick), the book geometry (the movie check places
  the pointer with the same function it tests; the lo-res doubling of the cell size is read from
  0x62a80, not observed), `creature_kill_all` against anything but its own lists.
- Not ported: joystick, VFX1, the pointer sprite, sound / music / video calls (requests only).

## Findings

- **The shipped recording was made in the 320x200 mode (DAT_0012edae == 1)** - steering statistics
  above. port_features.md found that the level features in the snapshot need `g_video_mode_flags != 1`
  (full-size castles). Both are true if the flag changed between level start and tick 413: it is set by
  the config and toggled in game by R (`video_toggle_resolution_33600` writes 8 / 1). So for a
  faithful replay of movie 0 the flag must be **1 while the packets are replayed** (castle footprints,
  `creatures.cpp` / `effects.cpp` / `level_features.cpp` all halve on `== 1`), although the static
  features of the snapshot were generated with another value. The port's default of 8 is right for
  generating level 38 like the snapshot and wrong for replaying the movie on top of it. Whether the
  two AI castles that exist at tick 413 are full or half size would date the switch.
- Dead code in the original: `ui_button_table_dispatch_17a80` has no caller; cmd 0x1c ("leave a lost
  level") can only be queued by the flight layer's Space handler, which never sees Space because the
  function-key layer consumes it first; the Alt+V / Alt+F1..F7 / Shift+Q / E / C / R / Space copies in
  the flight layer are shadowed the same way. Translated anyway.
- Nothing in 0x15590..0x17a80 queues cmd 5, 7, 8, 0xb, 0xe, 0x12 or 0x14 arg 1.
- The double-click variables of the mouse handler are never decremented or read.

## Deviations from the original

- `input_mouse_event` takes coordinates already in the 640-wide virtual screen (the `sar 3` of the
  640x480 driver scale is the caller's business); `input_mouse_move` also applies the driver's range
  (0..640, 0..400 / 0..480), which in DOS is the mouse driver's job.
- Guards where the original indexes unchecked: local player `& 7`, Thing indices `% 1000` / range
  checks, `Config.spell_slot` and the spell id used as indices (0..23), notice text bounded to 0x3f
  characters, the creature list walk bounded to the pool. A hand slot of 0xff (no spell) reads
  `P+0x214+0xff*4` = the next player's unused camera log in the original; emulated by reading the
  GameState at that offset (same as player.cpp) and falling back to things[0].
- The keyboard handler's port I/O, EOI, and the Tab hook in network games (FUN_0004cdf0(0, 0x3f, 0, 0),
  DAT_000adfca chain to the old vector) are omitted.
- `g_sky_available` stands for DAT_000adf48 != 0 and is 1; `g_sound_available` / `g_music_available`
  default to 0 (no audio in the port yet), so F1 / F2 do nothing until the platform sets them.
- `g_book_cell_w / h` default to the values of the shipped tab files by screen mode instead of being
  read from a loaded table.

## TODO(port) call sites

`input_joystick_poll_5a4e0(0, 0x100, 0xa0, 0x280, 0x190, 0x10, 8)` (flight) and
`(1, 0x280 - 4w, 0xa2, 0x280 - w, 0xa2 + 5h, w, h)` (book) -> `g_hook_input_joystick_poll`;
`sound_stop_all_5c040()`, `music_stop_1f960()`, `music_play_track_5c0a0(track)`,
`video_toggle_resolution_33600()`, `render_mapmode_palette_2ff50()`, `video_restore_mode_2ff10()` ->
`g_hook_input_platform(request, arg)`; pointer redraw `mouse_cursor_restore_vram_5b560` /
`mouse_cursor_draw_vram_5b050` in the mouse handler; the Tab debug hook in the keyboard handler.

## Hooks declared (input.h, null = skipped)

`g_hook_input_platform(InputPlatformRequest, int)`, `g_hook_input_joystick_poll(mode, x0, y0, x1, y1,
step_x, step_y)`, `g_hook_input_mouse_warp(x, y)`. Installed here: `g_hook_player_local_input`.

## (c) Requested shared-file changes

1. `sim.cpp` (`sim_init`) / `engine.cpp`: call `input_register_handlers()`; add `mcengine/input.cpp` to
   `MC_SIM_CORE` / the engine library. Platform: `input_reset()` + `input_text_load()` at start.
2. `player.cpp`, `player_cheat` case 7: replace the TODO by `creature_kill_all()` (input.h), or add
   `extern void (*g_hook_creature_kill_all)()` to player.h and let `input_register_handlers` install it.
3. `player.cpp`, `player_set_input_mode`: for the local player and mode != 2 call `input_mouse_center()`
   (the TODO there names input_mouse_center_4a000); through a hook in player.h if player.cpp must not
   include input.h. The recording shows the effect (51 of 54 closings).
4. `demo.cpp`: `g_hook_demo_input_changed = input_changed`; call `input_snapshot()` at the
   `TODO(port): input_snapshot_34060()`.
5. `player.cpp` language lines: the two TODO strings are table entries of `input_text()`:
   `*(char **)0xaddc0` = string 54 "has died.", `*(char **)0xadde0` = string 62 "has been eliminated from
   the realm." The table (`input_text_load` / `input_text`, 80 strings) wants a neutral home (e.g.
   `text.h`); it lives in input.cpp only because nobody owns it yet.
6. `mc_globals`: decide how `g_video_mode_flags` is handled for movie 0 (see "Findings": 1 during the
   replay). The movie part of input_test sets it to 1 locally.
7. HUD owner (later round): write `Config.spell_slot` from the book drawing code (then drop the
   `input_book_update_selection()` call), store the loaded table's sprite-3 size in `g_book_cell_w / h`,
   count the PlayerMsg ticks down.

## Corrections / additions for ENGINE.md, mc_types.h, carpet_types.txt

- Command packet: +1 arg, **+2 second argument** (cmd 0x17: book slot), +3 / +4 steer, +5 key bits
  (only under cmd 5 / 6). ENGINE.md's "+1 arg/bits" is wrong. `CmdPacket.pad2` -> `arg2`.
- `player_queue_command_17270(cmd, arg)`: rules in the table above; cmd 0x1b needs status & 2, 0x1c
  status & 4, 0xf health < 0 and state 3. The name test is 9 bytes at PlayerRec+0x40a, lower case.
- Input modes (PlayerRec+0x44a): 0 flight, 1 (Enter / both buttons leave it), 2 spell book, 3 chat
  line, 4 = handled like 0.
- Option bytes (agent_tick listed them without the keys): F4 +0x219d soften (`opt_smooth`), F5 +0x2195
  reflections (`opt_second_surface`), F6 +0x2197 sky, F7 +0x2196 shadows, F8 +0x2199 and +0x219a
  together ("Icons and Map"), F9 +0x219c speed blur 0 / 1 light / 2 heavy, F10 +0x219b 3D mode 0 / 1 /
  2, Alt+H +0x219e. `opt_allowed[]` (+0x21ad): [0] reflections, [1] shadows, [2] sky, [6] 3D mode,
  [7] speed blur, [8] soften, [10] resolution switch. View size +0x2198: `[` grows to 0x28, `]`
  shrinks to 0x11 (ENGINE.md has no direction).
- GameState+0x240 (i16): the level's music track (argument of music_play_track_5c0a0).
- Config: +0x16 `spell_slot` is the **book cell under the pointer** (index into DAT_00097660, 0xff =
  none), written by the HUD every frame; flags 0x200 = no spell book by double click (movie); flags
  0x8000 = cheat gate (Shift+E / C / F, Alt+V, cmd 0x1e without the name).
- PlayerRec.status: 2 = won (Shift+C sets it), 4 = lost (Shift+F), 0xc = restart (Shift+R, also cmd
  0xf without a castle). PlayerRec.flags: 8 = debug overlay; 0x10 / 0x20 are toggled by Alt+M / Alt+F
  and read nowhere.
- Thing (class 12): +0x3c == 1 marks a spell that fires on clicks only; +0x30 > 0 or (+0x3e != 0 and
  +0x3d > 0) keeps the fire bit alive while the button is held.
- Language strings: 80 pointers at 0xadce8 into data/{e,f,g,i}text.dat (NUL separated; 0..16 intro, 37..51
  option notices, 52..62 game messages, 63..65 speed, 75..78 sound / music). `DAT_000addxx` = string
  (addr - 0xadce8) / 4.
- Keyboard: DAT_0012eea0 is the raw last byte (break codes included), DAT_0012e396 the byte before,
  DAT_0012e397 the first make code. In-game text table DAT_0009e5e8[128] (upper case; entry 1 = 0x27).
- Mouse: driver range 0..0x1400 x 0..0xf00 (>> 3) in the 640x480 mode, 0..0x280 x 0..0x190 in 320x200
  (0x5be68, suggested name `mouse_set_range_5be68`); handler clamp 0x27e / 0x1de; start position
  (320, 200); DAT_0012ee06 / 08 double-click timer / flag (window DAT_0009e5e0 = 0x19, dead).
- HUD sprite table DAT_000adf94 / end DAT_000adf9c: resource lists 0x97294 (320x200: *WScreen 0x11580,
  data/mspr0-0.dat / .tab) and 0x97370 (640x480: *WScreen 0x4b000, data/hspr0-0.dat / .tab), chosen at
  0x33480 by `DAT_0012edae == 1`; 0x49de0 relocates the tab list 0x9744c with 0x62a80 (lo-res: also
  **doubles every width / height byte**) or 0x62ad0. All UI coordinates are in the 640-wide virtual
  screen and the blitter (0x6070d) halves position and size when DAT_0012edae & 1.
- `ui_button_table_dispatch_17a80(table)`: 0x18-byte records {i16 x0, y0, +6 x1, +8 y1, +0xc mouse
  condition 0..0xf, -1 = none (jump table 0x17a40: 0 pointer inside, 1 left click, 2 right click, 3
  either click, 5 left held, 6 right held, 7 either held, 0xb both clicks, 0xf both held; clicks test
  the click position, the others the pointer), +0xd key (compared with the last key), +0xf / +0x11 key-table index
  with +0x10 / +0x12 wanted state, +0x13 command (0 ends the list), +0x14 arg}; calls
  player_queue_command(cmd, arg); no caller.
- `player_set_input_mode_3bb50` (named chat_message_show): confirmed from this side (pointer sprite
  +6 for the book, input_mouse_center otherwise).
- agent_tick's description of player_local_input ("R = toggle res; Space; I; 1-0 spells; Enter; arrows")
  is complete only with the modifier layers above; "mode 1" exists.
