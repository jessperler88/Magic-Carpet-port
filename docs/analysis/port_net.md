# Port round 6, task B: network play (NetBIOS over a transport, TCP for mcport) and front-end leftovers

Files: `src/mcengine/net.h/.cpp` (contract kept, additions below it), `src/mcport/net_tcp.h/.cpp` (new),
`src/mcengine/frontend.h/.cpp` (lobby, sound summary, in-game pointer API), `src/mcengine/game.h/.cpp`
(pointer hook, mapmode palette calls, desync arm), `src/tests/net_test.cpp/.cmake`. Build dir `build_B`.
`net_test` exits 0 with zero warnings (/W4), also through `ctest -R net_test` (8 s). `frontend_test`,
`game_test`, `sim_test` and `reference_test` (movie 0, 0 divergences) still pass in `build_B`; `mcport` builds
without warnings, and a scratch copy of mcport with the integration code below compiled with zero warnings
and started twice with `network` (first instance: name server; second: connected to it).

## How the original plays over a network

- `carpet -network`: config_parse_33750 calls `net_init_4ee70` only. It runs `system("netbios")` (string
  0x92e48, loads the DOS NetBIOS driver), allocates the NCBs and buffers in DOS memory, tests for NetBIOS
  (`net_netbios_detect_4e8f0`: int 5Ch vector + command 0x7f answered with 3) and sets DAT_0009e3c8 = 1. That
  flag is what the front end calls `g_fe_network`: it enables main-menu item 3, the lobby.
- The command-line join that ENGINE.md lists for `network` (players > 1 -> net_join_session, state+8, flags
  |= 0x10) is **dead code**: the option sets the local `[esp+0x90]`, and 0x33c3a..0x33c41 clears it right after
  `net_init` (`xor ebp, ebp; call 0x4ee70; mov [esp+0x90], ebp`), so the test at 0x33ef6 never passes.
- A game is joined in the lobby (`fe_screen_multiplayer_54bd0`, state 4): session name `CARPET<n>`
  (Config+0x75, clicking the name cycles n 0..9), player count by clicking a slot (DAT_0012ed31, 2..8), map =
  level 0x32 + selected line (50..69). "Start" calls `net_session_join_4f030(Config+0x75, count)`; on success:
  Config.flags |= 0x10, `commands[local].arg = Config.level`, `net_exchange_frame(commands, 10)`, Config.level
  = `(int8)commands[0].arg` (**player 0's map wins**), DAT_0009e504 = 1 (leave the front end). The screen
  state stays 4: after a network level the front end shows the lobby again (no result screen). On -1 the
  local player number (state+8) becomes -1, the lobby is redrawn and faded in.
- In the level, `player_commands_process_3a8b0` (Config.flags & 0x10) exchanges the 10-byte command packets
  every tick and, in the tick a player joins (command 1), the 0x801-byte player records. Commands 2 / 0x1a /
  0x1b / 0x1c / 0x1d / 0xf-without-castle call `net_player_disconnect_4f360` for the player that leaves.

### NetBIOS layer (0x4e4b0..0x4f6e0)

- Globals: DAT_0009e3c8 present, 9e3c9 joined, 9e3ca local player, 9e3cc player count, 9e3f8 abort, 9e3fa
  host, 9e3fc control NCB, 9e3d0 / 9e3d4 send / receive buffers (0x800), 9e3d8[8] per-player buffers (only
  used by the unused 4ec10), 9e400[8] one NCB (0x42 bytes) per player slot, 9e420[8] status (2 local,
  1 session up, 0 none), 9e428 session name.
- Every command is the **no-wait** variant (0xb0 add name, 0xb1 delete name, 0x90 call, 0x91 listen, 0x92
  hang up, 0x94 send, 0x95 receive; 0x35 cancel) and the code busy-waits on cmd_cplt (+0x31) == 0xff. rto /
  sto (+0x2a / +0x2b) are 0: no timeouts.
- Names: `sprintf("%s%d", session, slot)` (0x92e40) padded with spaces (0x92e3c) to 15 characters, e.g.
  `CARPET00`. **The player number is the first slot whose name could be added** (`net_add_name_4e530` in a
  loop over 0..7: 0 -> this slot; 0x0d "duplicate in the local table" -> delete it and retry the slot; any
  other error, e.g. 0x16 "in use on the network" -> next slot).
- Then every slot NCB gets the local name, a LISTEN is posted for every other player's name, and every
  player whose LISTEN is still open is CALLed from the local slot's NCB (`net_call_4e600`: waits until the
  call or that LISTEN completes, cancels the other, and moves a successful call's session into the
  player's slot). Finally it waits until every slot has a session. That is a **full mesh**: every pair of
  players has its own session. The host is player 0.
- `net_exchange_frame_4f530(base, size)`: the host receives `size` bytes from every other player into
  `base + p * size` (in player order), then sends `count * size` bytes to every other player; a client sends
  its block to the host and receives `count * size` bytes. Messages over 0x800 bytes are split into 0x800-byte
  messages plus the remainder (also when it is 0). A failed send / receive re-posts the slot's LISTEN.
- `net_player_disconnect_4f360(p)`: the leaving player hangs up all its sessions and deletes its name
  (joined = 0); everybody else cancels and hangs up p's slot. Then the status table is rebuilt and **the host
  becomes the first player still connected**. Because the leave is a command packet, every instance runs
  it in the same tick, so the new host is the same everywhere (tested below).
- `net_exchange_block_4f620` (all-to-all, each player broadcasts its block in turn) has no callers.
  `net_receive_player_4ec10` / `net_receive_player_wrap_4ec50` (a no-wait RECEIVE into DAT_0009e3d8[p]) have
  no live caller either.
- Lobby callbacks: `fe_lobby_slot_connecting_55920` (slot anim 1 = blinking, from the add-name and call
  waits), `fe_multiplayer_slot_joined_559c0` (anim 2, name added / session up, and every pass of the final
  wait), `fe_lobby_slot_clear_55a60` (anim 0, name failed). Each increments DAT_0012ed10 and redraws.
- `net_check_cancel_4e4b0`: Esc (g_key_last == 1) or a left click in 640-space 0x238..0x25e x 0x60..0x86 sets
  the abort flag, clears key and click and fades to black (`vga_palette_fade_61510(NULL, 0x10, 0)`);
  otherwise the click is consumed. That box is the lobby's top-right button (320-space 0x11c..0x12f x
  0x30..0x43), which has no action outside the join: it is the **abort-join button**.

## Functions translated

| original | port |
|---|---|
| net_check_cancel_4e4b0 | `net_check_cancel` (net.cpp) + the input / fade half `fe_lobby_cancel_requested` (frontend.cpp, through `NetLobbyHooks::cancel_requested`) |
| net_add_name_4e530 | join phases `J_NAME_START` / `J_NAME_WAIT` + `join_name_result` |
| net_call_4e600 | join phases `J_CALLS` / `J_CALL_WAIT` + `join_call_result` |
| net_cancel_4e870 | `net_cancel` |
| net_netbios_detect_4e8f0 | `net_present_test` (+ `NetTransport::present`) |
| net_delete_name_4e940, net_hangup_4e9e0, net_listen_4ea10 | `net_delete_name`, `net_hangup`, `net_listen` |
| net_receive_4eb30, net_receive_chunked_4ebb0 | `net_receive`, `net_receive_chunked` |
| net_send_4ec80, net_send_chunked_4ecf0 | `net_send`, `net_send_chunked` |
| net_build_player_status_4ed50, net_player_status_4ed70 | `net_build_player_status`, `net_player_status_of` |
| net_netbios_submit_4eda0 | `submit` -> `NetTransport::submit` |
| net_init_4ee70, net_shutdown_4efc0 | `net_init`, `net_shutdown` |
| net_session_join_4f030 | `net_session_join` = `net_session_join_begin` + `net_session_join_step` until done |
| net_player_disconnect_4f360 | `net_player_disconnect` |
| net_send_to_player_4f470, net_receive_from_player_4f4d0 | `net_send_to_player`, `net_receive_from_player` |
| net_exchange_frame_4f530, net_exchange_block_4f620 | `net_exchange_frame`, `net_exchange_block` |
| fe_screen_multiplayer_54bd0 (0x54d86..0x54f22: start, join, level exchange; 0x551a4: exit without restore after a start) | `fe_screen_multiplayer` phases 5 / 6, `mp_join_done` |
| fe_lobby_slot_connecting_55920, fe_multiplayer_slot_joined_559c0, fe_lobby_slot_clear_55a60 | `fe_lobby_slot_connecting` / `_connected` / `_clear` |
| fe_sndsetup_read_57af0 | `fe_sndsetup_read` (reads `<save_dir>/sndsetup.dat` when `sndsetup.inf` exists) |
| fe_config_draw_summary_52e20 (texts 23..26) | the four lines in `fe_config_draw_summary` |
| mouse_cursor_set_sprite_5ba5c (in-game table data/pointers, DAT_000adfc0) | `mouse_cursor_set_pointer` / `mouse_cursor_pointer` / `mouse_cursor_draw` (frontend.h) |
| game_main_32a00 0x32b6a, video_toggle_resolution_33600 0x33726 (pointer 0) | `g_hook_game_mouse_cursor(0)` in `game_after_frontend` / `game_toggle_resolution` |
| game_main_32a00 0x32beb..0x32bfa (mapmode_palette_save / restore around the switch back to 640x480) | `game_after_frontend` (the round-5 TODO) |

## Design

- **NetBIOS model** (net.h): `NetNcb` mirrors the NCB fields the game uses (cmd, retcode, lsn, num, buffer,
  length, callname, name, rto, sto, cmd_cplt, plus `cancel_target` for 0x35). `NetTransport { present;
  submit(ncb); poll(timeout_ms); close_all }` is the int 5Ch driver: `submit` starts a command (cplt = 0xff or
  the result), `poll` completes pending ones. net.cpp is a line-by-line translation of the NCB sequences; every
  busy-wait of the original is `while (cplt == 0xff) poll()` (10 ms select wait).
- **Frame-stepped join**: the lobby must keep drawing, so `net_session_join_4f030`'s loops are phases of a
  small state machine. `net_session_join_step()` runs until the next wait and returns
  `NET_JOIN_PENDING` (the lobby calls it once per `fe_frame`, poll timeout 0). The blocking contract function
  `net_session_join` steps until done. The callbacks only set the slot animation and DAT_0012ed10; the lobby
  redraws once per frame.
- **Contexts**: all of the DAT_0009e3c8.. state lives in a `NetContext`; a thread-local pointer selects it
  (default: one per process). Only the test uses more than one (simulated peers in threads).
- **TCP transport** (`mcport/net_tcp.*`, Winsock, BSD sockets elsewhere, no SDL): one instance is the name
  server on MC_NET_PORT (default 30600). Without MC_NET_HOST an instance binds the port (SO_EXCLUSIVEADDRUSE)
  or, if another instance on the machine has it, connects to 127.0.0.1; with MC_NET_HOST it connects there.
  Every instance also listens on an ephemeral session port. ADD NAME / DELETE NAME / the name lookup of CALL go
  to the name server; a CALL connects **directly** to the called instance (so the mesh does not depend on the
  name server once it is built, and the game survives the name server's player leaving). Frames on every
  stream: `u32 length, u8 type, payload`; a session carries one CALL_REQ / CALL_ACK handshake, then one DATA
  frame per NetBIOS message; closing the socket is the hang-up (pending / later RECEIVEs end with 0x0a). TCP_NODELAY.
  A CALL that arrives without a matching LISTEN is rejected (0x12). Two players calling each other at the same
  time: only the call from the smaller name is accepted, and a second session between the same two names is
  refused, so every pair ends up with exactly one session (with real NetBIOS the original could in principle
  end up with two crossing sessions; the port's rule keeps the original's join code unchanged and safe).
- **Desync check** (port only, `net_sync_*`): while armed (`game_level_begin` arms it, a join disarms it),
  every command exchange is followed by one extra 8-byte message per direction on the same sessions: the
  client sends `{exchange number, checksum}` after its packet, the host answers with its own pair after the
  array. Both compare and count; the first mismatch is printed (`net: DESYNC at exchange n ...`), nothing
  else happens. Interval MC_NET_SYNC (default 1 = every tick, 0 = off: then the wire carries only the
  original's messages); all instances of a session must use the same interval. `net_state_checksum()` is
  FNV-1a over GameState.rng, g_rng16, the free / recyclable stacks, the Thing pool, the player records' head
  fields and P blocks, and the five maps (~0.5 ms per tick in Debug), **minus what the original itself keeps
  per instance** (found with the two-process test, see below).

## Verification (net_test, `build_B/Debug/net_test.exe`, all numbers from the last run)

1. **In-memory NetBIOS, 3 peers in 3 threads** (`MemLan` in the test: names, LISTEN / CALL with the same
   crossing-call rule, message sessions, cancel, hang-up; completions in the owner's poll):
   - join: player numbers 0 1 2 (distinct; the first to add its name is 0), host 0;
   - 0x801-byte record exchange (0x800 + 1-byte messages, the host sends 3 * 0x801): every byte right;
   - 50 rounds of 10-byte packets: every slot right in every peer; **order** on the host
     `R1 R1 R2 R2 S1 S1 S2 S2` (packet + sync message per client: receive from all in player order, then send
     to all), on each client `S0 S0 R0 R0`;
   - desync side channel: player 2 reports a different checksum at exchange 20 -> host: 100 checks,
     1 mismatch at 20; client 2: 50 checks, 1 mismatch at 20; client 1: 50 checks, 0 mismatches;
   - graceful leave of the host (`net_player_disconnect(0)` on every peer, as command 0x1d does): host 1 on the
     remaining peers (0 on the leaver itself, which only has itself); 10 more rounds between 1 and 2, slot 0
     empty;
   - player 2 crashes (transport closed, no disconnect): host 1 goes on for 5 rounds, slot 2 empty, its failed
     receives re-post the LISTEN as the original does;
   - aborted join (2 of 3 players, cancel hook): both return -1 and no name is left in the name table.
2. **Two processes over TCP, 2000 ticks**: the test starts itself again (`--peer`); both join `NETTEST`, run
   the lobby's level exchange (player 0 chose 50, player 1 chose 51: both load 50), set the name strings as the
   lobby's name dialog would, load level 50 with Config.flags 0x10, and run 2000 ticks of `game_tick_sim` with
   scripted mouse steering, both mouse buttons (casts), Up held / released and Space for their own player
   (`player_local_input`). `g_timer_ticks` differs per process (real time). Result: **2000 / 2000 per-tick
   checksums identical** between the processes (files compared afterwards), the online side channel compared
   2000 checksums per player with 0 mismatches, 2000 distinct checksums in 2000 ticks (the state changes every
   tick), both players moved (e.g. player 1 ends at dc5f,a5c6), 3.2 s.
3. **Three processes, the host leaves**: as 2 with 3 players; player 0 (the host, also the name server)
   queues command 0x1d at tick 1200. All three process it in tick 1200; players 1 and 2 continue with **player 1
   as host** to tick 2000: 3201 per-tick checksum pairs compared, 0 differ; the side channel: host 0 2402
   checks, players 1 / 2 2000 each, 0 mismatches. 5.7 s.
4. **The lobby** (frontend.cpp) through the in-memory transport, joining as player 1 while player 0 waits and
   answers slowly: the frame-stepped join (31 frames), slot 0 blinks while the CALL is pending (looked at
   `net_lobby_joining_a.ppm`: slot 0 in its "off" phase, slot 1 lit), both slots lit at the end
   (`net_lobby_joined.ppm`), the level exchange gives level 51 (player 0's) to both, Config.flags 0x10, local
   player 1, player count 2, status 1 / 2, host 0, `FE_START_LEVEL`.
5. **Sound summary**: with `sndsetup.inf` + a hand-built `sndsetup.dat` in the save directory the config
   screen shows the card names and the lines "Sound I/O : 220", "Sound IRQ : 5", "Sound DMA : 1",
   "Music I/O : 388" (texts 23..26; looked at `net_config_sndsetup.ppm`); 612 pixels of the summary box differ
   from the default screen.

What the two-process run found the original keeps **per instance** in GameState (left out of the checksum,
listed in net.h): GameState.local_player; PlayerRec.tick (only the local record counts), messages, camera log,
name (with an empty Config+0x1d the local record's name becomes the empty call-name locally only - cosmetic,
original behaviour); PlayerBlock.start_tick (real time); the HUD flash counters PlayerBlock +0x187..+0x189 and
spell_flash[] (hud_tick_state counts them down for the local player only; not exercised by the test, which
runs without the renderer); Thing.flags bit 0 of the human flyers (player_spawn_3f360 marks the local one) and
of the spells (class 12, "the local player owns it"). The one sim function that reads the local player's
position, `creature_proximity_wake_timer_46960`, is switched off by the original in network games
(thing.cpp `!(Config.flags & 0x10)`): the original was built for lockstep.

Not verified: a session across two machines (only localhost); two real mcport windows through the GUI lobby
into a level (the lobby + join + exchange are tested in-process, the level in lockstep in two processes
without the renderer); the original's own NetBIOS traffic (no DOS NetBIOS under DOSBox here).

## Deviations / gaps

- Transport instead of int 5Ch; names, sessions and message semantics as NetBIOS. Differences: a CALL to a
  name without a pending LISTEN is rejected at once (NetBIOS behaves the same: 0x12); crossing calls resolved
  by name order (see Design); messages over the receive buffer are truncated with 0x06 (the game never sends
  more than 0x800 per message).
- `net_session_join_4f030`'s abort after the names loop deletes the name through slot `local + 1` (for local 7
  that is the status table). The port uses slot `local` (the slot does not matter to NetBIOS).
- `net_shutdown` also closes the transport and clears DAT_0009e3c9 (the original leaves it set; it only shuts
  down at program end).
- `net_init` does not leak the control NCB when the presence test fails (the original does).
- A peer that vanishes without closing its socket blocks the others' receive forever (rto / sto 0, as in the
  original). A peer whose socket closes: its slot stays empty, the host goes on. If the **host** crashes (no
  leave command), the clients' arrays diverge: the side channel reports it; the original has no recovery either.
- mcport is unpaced by the network: every instance ticks at MC_TICK_HZ and the exchange waits for the
  slowest; while it waits the idle hook pumps SDL events.
- The per-tick checksum costs ~0.5 ms (Debug) per tick; MC_NET_SYNC=0 switches it off.
- Front end: after a network level mcport should not `fe_enter(5)` (the original shows the lobby again, see the
  integration code).

## The front end in 640x480 (closed)

Task D settled it (port_render_reference2.md, top): DAT_0012edae is written only by config_parse_33750 (always
1) and video_toggle_resolution_33600, and game_main_32a00 switches a 640x480 game to 320x200 right before the
only call of frontend_menu_loop_52070 (0x32af4..0x32b0b; port_game.md's structure shows the same). Every
`DAT_0012edae != 1` branch in the fe_* code is dead at run time; `fe_frame` forcing mode 1 is exact. frontend.h's
TODO is replaced by that statement.

## Hooks declared / installed

- `NetTransport` (net.h), installed per context with `net_set_transport`.
- `NetLobbyHooks { slot_connecting, slot_connected, slot_clear, cancel_requested, idle }` (net.h): the front end
  installs the first four when the lobby opens and keeps an `idle` the platform set before.
- `net_sync_set_checksum(fn)` (tests).
- `g_hook_game_mouse_cursor` (game.h, defined in game.cpp): mcport -> `mouse_cursor_set_pointer`.
- Requested: `g_hook_player_mouse_cursor` (player.h / player.cpp, below).

## Extra API (additions)

net.h: the NetBIOS enums and `NetNcb`, `NetTransport`, `net_set_transport` / `net_transport`, `NetLobbyHooks`,
`net_set_lobby_hooks` / `net_lobby_hooks`, `net_check_cancel`, `NET_JOIN_PENDING`, `net_session_join_begin` /
`_step`, `net_exchange_block`, state getters (`net_present`, `net_joined`, `net_local_player`,
`net_player_count`, `net_host`, `net_aborted`, `net_player_status`), the desync check (`net_sync_set_interval`,
`net_sync_arm`, `net_sync_disarm`, `NetSyncStats` / `net_sync_stats`, `net_state_checksum`,
`net_sync_set_checksum`), contexts (`net_context_create` / `destroy` / `select`). net_tcp.h: `TcpTransport`,
`net_tcp_from_env(force)`. frontend.h: `mouse_cursor_set_pointer`, `mouse_cursor_pointer`, `mouse_cursor_draw`.
game.h: `g_hook_game_mouse_cursor`.

## TODO(port) call sites left

None in net.cpp / net_tcp.cpp. frontend.cpp: joystick / VFX1 device init (`fe_config_apply_input_device`,
unchanged). game.cpp: the memory-manager calls (unchanged, not needed).

## Requested shared-file changes

1. `src/mcengine/player.h`, next to the other hooks:
   ```cpp
   // mouse_cursor_set_sprite_5ba5c from player_set_input_mode_3bb50: pointers entry 1 (the spell book) in input
   // mode 2, entry 0 otherwise (null = no pointer handling). mcport: mouse_cursor_set_pointer (frontend.h).
   extern void (*g_hook_player_mouse_cursor)(int entry);
   ```
2. `src/mcengine/player.cpp` (task C's file), definition near the top with the other globals:
   ```cpp
   void (*g_hook_player_mouse_cursor)(int entry) = nullptr;
   ```
   and in `player_set_input_mode`, replace the line `// TODO(port): mouse_cursor_set_sprite_5ba5c (book cursor in mode 2)` with
   ```cpp
        // mouse_cursor_set_sprite_5ba5c(DAT_000adfc0 + 6) in mode 2, (DAT_000adfc0) otherwise (0x3bb6f..0x3bb97)
        if (g_hook_player_mouse_cursor) g_hook_player_mouse_cursor(mode == 2 ? 1 : 0);
   ```
   (No game state is touched: all reference tests stay identical.)
3. `src/mcport/main.cpp`: the integration below.
4. Nothing in CMake: `mcport/net_tcp.cpp` is picked up by the mcport glob and mcport already links ws2_32; the
   test links ws2_32 in its own .cmake.

## mcport integration (exact code for src/mcport/main.cpp)

The same edits as `<scratch>/round6_B/patch_main.py`, which applied them to a copy and built it with zero
warnings.

1. Includes, after `#include "mcfile.h"`:
   ```cpp
   #include "net.h"
   #include "net_tcp.h"
   ```
2. The `network` argument (config_parse_33750's option) must not become a viewer level. Replace
   `const char *mode = argc > 2 ? argv[2] : "";` with
   ```cpp
       // `network` / `-network` (config_parse_33750's option: net_init_4ee70 only, the join is the lobby's)
       const bool net_arg = argc > 2 && (std::strcmp(argv[2], "network") == 0 || std::strcmp(argv[2], "-network") == 0);
       const char *mode = (argc > 2 && !net_arg) ? argv[2] : "";
   ```
3. After `g_hook_game_title_screen = [] { title_screen_show(s_game.c_str(), frame()); };`:
   ```cpp
       g_hook_game_mouse_cursor = mouse_cursor_set_pointer;         // mouse_cursor_set_sprite_5ba5c (game.h)
       g_hook_player_mouse_cursor = mouse_cursor_set_pointer;       // ... from player_set_input_mode_3bb50 (player.h)
       mouse_cursor_set_pointer(0);                                 // video_input_init_3ed60: pointers[0]

       // Network: `mcport <dir> network`, MC_NET=1 or MC_NET_HOST=<name server> (MC_NET_PORT, default 30600).
       // net_init_4ee70 makes the lobby (main-menu item 3) available; the join happens there.
       TcpTransport *net = net_tcp_from_env(net_arg);
       if (net) {
           net_set_transport(net);
           NetLobbyHooks nh = net_lobby_hooks();
           nh.idle = [] { SDL_PumpEvents(); };                      // a waiting exchange keeps the window alive
           net_set_lobby_hooks(nh);
           if (const char *e = SDL_getenv("MC_NET_SYNC")) net_sync_set_interval(std::atoi(e));
           if (net_init() == 1) {
               g_fe_network = 1;                                    // DAT_0009e3c8
               std::printf("network: %s, sessions on port %u\n", net->is_name_server() ? "name server" : "connected to the name server",
                           (unsigned)net->session_port());
           } else {
               std::fprintf(stderr, "network: %s\n", net->last_error());
           }
       }
   ```
4. After a level, a network game goes back to the lobby (the original keeps screen state 4). In the RUN_LEVEL
   branch replace `fe_enter(5);              // the level result screen` with
   ```cpp
                       if (!(g_cfg->flags & 0x10)) fe_enter(5);   // the level result screen (a network game: the lobby again)
   ```
5. The pointer: replace the block
   ```cpp
                   if (run == RUN_LEVEL && g_state->players[g_state->local_player & 7].input_mode == 2) {
                       ui_set_target(frame());
                       ui_draw_sprite(g_mouse_x, g_mouse_y, ui_sprite(g_ui_pointers, 1));
                   }
   ```
   with
   ```cpp
                   if (run == RUN_LEVEL) mouse_cursor_draw(frame());
   ```
   (needs the player.cpp change; until then the book pointer is not selected).
6. At the end, before `audio_shutdown();`:
   ```cpp
       net_shutdown();
       delete net;
   ```
7. Usage line for the header comment: `mcport [game_dir] network` - the multiplayer lobby (main menu item 3);
   MC_NET_HOST / MC_NET_PORT select the name server (the first instance on a machine becomes it), MC_NET_SYNC=n
   compares the game state every n ticks (0 = off). On Windows the first start asks the firewall for the two
   listening ports.

## Corrections to ENGINE.md / names (carpet_names.csv comments)

- ENGINE.md "Command line": `network` only calls `net_init_4ee70`; the join with `players` / state+8 /
  flags |= 0x10 after the parse loop is unreachable (0x33c41 clears the option flag). `net_init_4ee70` first runs
  `system("netbios")`.
- ENGINE.md "Corrections to existing notes": "Network = cfg bit 0x80" is wrong: 0x80 is the `debug` option. A
  network game is **Config.flags 0x10**, set by the lobby after a successful join (input.cpp's
  `net_or_custom` and player_commands_process_3a8b0 test it).
- `net_session_join_4f030` comment ("default name TESTER; session names Game One..Game Six (0x9E469)"): wrong -
  0x9e469 are the save-slot default names. NetBIOS names are `"%s%d"` of the session (Config+0x75, `CARPET<n>`)
  and the slot, padded with spaces to 15; the slot number of the first name that could be added is the player
  number; then LISTEN for all / CALL all / wait for all (full mesh); host = player 0.
- `net_exchange_frame_4f530` comment has the helpers swapped: receive is 4f4d0, send is 4f470.
- `net_receive_from_player_4f4d0` receives through `net_receive_chunked_4ebb0` (not 4ec10); 4ec10 / 4ec50 are
  unused.
- `fe_multiplayer_slot_joined_559c0`: the NetBIOS "connected" progress callback (slot anim 2) of add name /
  call / the final wait of the join; a better name is `fe_lobby_slot_connected_559c0`.
- `net_check_cancel_4e4b0`'s box is the lobby's top-right button (abort while joining).
- fe_screen_multiplayer_54bd0 after a successful join: Config.flags |= 0x10, commands[local].arg =
  Config.level, net_exchange_frame(commands, 10), Config.level = (int8)commands[0].arg, DAT_0009e504 = 1, the
  player record / level / flags are **not** restored on the way out and the screen stays 4. After a failed
  join state+8 = -1.
- All NetBIOS commands of the game are the no-wait variants (0x80 bit), including delete name, hang up, send
  and receive; rto / sto are 0.
- fe_sndsetup_read_57af0: `c:\carpet.cd\sndsetup.inf` must exist; then `sndsetup.dat` = card id (0x20), card
  name (0x20), music id (0x20), music name (0x20), eight 10-byte fields (DAT_0012ebfe, ebe0 = sound I/O, ec12,
  ebea = IRQ, ec26, ec30 = DMA, ec08, ebf4 = music port). fe_config_draw_summary_52e20 draws `"%s : %s\n"`
  (0x931c0) lines of texts 23..25 with I/O / IRQ / DMA when the card id is not "NONE", and text 26 with the music
  port when the music id is not "NONE". (FORMATS.md material.)
- mouse_cursor_set_sprite_5ba5c callers in the game: game_main 0x32b6a and video_toggle_resolution_33600
  0x33726 set pointers[0]; player_set_input_mode_3bb50 sets pointers[1] (book, mode 2) / [0].
- The front end never runs in 640x480 (see above).
