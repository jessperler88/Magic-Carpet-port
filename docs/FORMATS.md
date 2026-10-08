# Magic Carpet 1 data formats (as established by `tools/mctools`)

All offsets little-endian unless noted. Everything here was verified against the
retail CD files in `MagicCarpet/magic` on 2026-10-06.

## Executables

| File | Size | Notes |
|---|---|---|
| `carpet.exe` | 866,133 | Retail game. MZ stub (DOS/4GW bound) + LE header at 0x28FEC. Launched by DOSBox (`carpet`). Credits strings present. |
| `data/main.exe` | 914,773 | Alternate build of the same game (Gravis Ultrasound / AdLib Gold strings, "MWANDER" debug text, no credits). Useful for diffing. |
| `maphack.exe` | 192,814 | Bullfrog's level tool. Tiny LE (2 objects). |
| `dos4gw.exe` | 265,396 | DOS extender. |

LE layout of `carpet.exe` (page size 4096, 138 pages, 15,930 fixups, all
32-bit offset fixups, so the program is a flat 32-bit image):

| Object | Base | Size | Flags |
|---|---|---|---|
| 1 | 0x00010000 | 0x6B9EE | R X (code + rodata) |
| 2 | 0x00080000 | 0x130 | R W (tiny; DOS/4GW glue) |
| 3 | 0x00090000 | 0xA2A70 | R W (data + bss, initial ESP = 0x132A70) |
| 4 | 0x00140000 | 0x304 | R W |

Entry point 0x000624C8 (Watcom C runtime startup).

Bound DOS/4GW layout: `<DOS/4GW stub MZ exe><original wlink output>`. The original
file keeps its own small MZ header (at 0x26654 in carpet.exe) whose `e_lfanew`
(0x2998) points at the LE header (0x28FEC). The LE header's *file-relative*
fields, notably the data pages offset (0x23E00), are relative to that inner MZ
header, so pages start at 0x4A454. Header-relative fields (object table, page
map, fixup tables) are relative to the LE header as usual. `lefile.py` handles
this and its flat image is byte-identical to Ghidra's loaded memory. `main.exe` has the same
object layout but data starts at 0xA0000. `tools/mctools/lefile.py` dumps the
map, fixups and a flat relocated image (`extracted/exe/*.flat.bin`).

## RNC ProPack method 1

245 of the 337 files start with `RNC\x01`. 18-byte big-endian header: unpacked
length, packed length, CRC-16 (poly 0xA001) of unpacked and packed data, leeway,
chunk count. `tools/mctools/rnc.py` is a faithful dernc port validated
byte-for-byte against the `propack` package on every file.

Files that are **not** compressed: `*.tab` for levels/pointers/screens sprites,
the 64,000-byte full-screen images (`screens/*.dat` = 320x200 8-bit) and their
768-byte `.pal`, `intro/*.dat` movies, `movie/*.dat`, `etext/ftext/gtext/itext.dat`,
`bullfrog.lbm` (IFF ILBM), `tables.dat`-generated data, `CARPET.CD/*`.

## Levels

- `levels/levels.tab`: 1000 x u32 offsets into `levels.dat`; 70 valid.
- `levels/levels.dat`: concatenated RNC level blobs (identical to the loose
  `levXXXXX.dat` files, verified for all 70).
- Decompressed level = 38,812 bytes:

| Offset | Size | Content |
|---|---|---|
| 0x0000 | 48 | 12 x int32: unk0, Seed, Off, Raise, Gnarl, River, Sourc, SnLin, SnFlt, BhLin, BhFlt, RkSte |
| 0x0030 | 1042 | zeros |
| 0x0442 | 35,982 | 1999 x THING_INIT (u16 Class, Model, Xpos, Ypos, DisId, SwiSz, SwiId, Parent, Child); the exe's spawn loops stop at 0x90d0 |
| 0x90d0 | 1,728 | 8 x 0xd8-byte per-player block (GameState+0x385d3 + p*0xd8): +4 AI aggression, +8 reaction, +0xc accuracy (copied to P+0x20a / +0x20e / +0x20c of computer players), +0x10[24] spells the player starts with, +0x74[24] spells allowed. *Corrected (round 5 merge): an earlier version said "castle position at +4" (port_player.md).* Earlier notes counted these as 96 more THING_INIT records. |
| 0x9790 | 12 | footer: u16 win percent, u16 player count, 8 x u8 castle level per player |

Class/model tables are in `tools/mctools/level.py`. `levels/gam00088.dat` is a
*text* save of level 65 with the same fields (GEN_MAP / THING_INIT), the key
that named the fields. Terrain is generated at run time from the 11 parameters;
no heightmap is stored.

- `levels/levXXXXX.inf`: RNC text, entity counts per level.
- `levels/state.rst`: RNC text, 239 bytes, editor state (`[edit-]`, `screen=`, paths).

## Sprites (`.dat` + `.tab`)

`.tab` = N x 6 bytes: u32 offset, u8 width, u8 height (entry 0 is empty).
Both files may be RNC compressed independently. Pixel data per sprite is a
sequence of rows; each row is runs terminated by 0:

    n = int8: n > 0 copy n pixels, n < 0 skip -n (transparent), 0 end of row

followed by one trailing byte after the last row. Verified visually on
`hspr0-0` (HUD, spell icons, balloons, Bullfrog logo). Sets:
`hspr0-0` (hi-res HUD, 86), `mspr0-0` (lo-res HUD, 86), `font0/1/2`,
`pointers` (8 cursors), `building` (68), `screens/{mmspr,sfont0-2,confspr,sptrs,gcspr,pmultspr,langspr}`.

## Texture maps / billboards (`data/tmaps.dat` + `tmaps.tab`)

- `tmaps.dat`: 8-byte tag `BULLFROG` then the RNC chunks back to back.
- `tmaps.tab`: 530 x 10 bytes: u32 unpacked_size, u32 offset (of the RNC
  header in `tmaps.dat`), u16 group. 529 sprites plus a sentinel record {0, file size, 0}. The
  group id is the index of the group's first sprite (port_sprites.md).
- Each chunk: u8 flags, u8 draw type, u16 width, u16 height, then pixels. *Corrected (round 5
  merge): byte 0 was called "kind" and byte 1 "unk"; byte 0 bit 0 = animated (bit 3 is set at run
  time = drawn since the last texture_anim_update), byte 1 = the draw type the loader copies into the
  sprite descriptor.*
  - byte 0 = 2 (316 chunks): exactly width*height 8-bit pixels (palette.dat).
  - byte 0 = 3 (213 chunks, animated): width*height pixels of frame 0, then {u16 frame count, u32
    size} and Autodesk FLC frames (0xF1FA, SS2 delta chunks) applied in place by
    `texture_anim_update_4be50` / `fli_decode_ss2_50f71` (a fill count of 0 means 256 words).
- Chunk 0 is a wizard on a carpet (80x67). These are the creature/object
  billboard frames.

## Palettes

`data/palette.dat` (RNC, 768 bytes) is the in-game palette, 6-bit VGA
components. `screens/*.pal`, `book.pal`, `smaltit.pal`, `smatitle.pal` are
screen-specific. Rendered swatches in `extracted/palettes/`.

## Other data (not yet decoded)

| File | Unpacked | Guess |
|---|---|---|
| `tables.dat` | 83,456 | Image of 0xb99b0..0xcdfb0: shade table (+0, 64 rows x 256: rows 0..0x1f tint toward palette entry 255, 0x20 identity, 0x21..0x3f toward black), blend table (+0x4000, blend[a<<8|b] = one third from b toward a), per-texture average colours (+0x14000, + copy at +0x80), circle profile (+0x14300, isqrt(0x10000-i*i), [0]=0xff). Regenerated by the exe when missing (tables_load_or_generate_3eaa0); the port reproduces shade/blend/circle byte-exactly (`tables_test`). |
| `sky.dat` | 65,536 | 256x256 8-bit sky texture, sampled as sky[(v<<8)|u]; 256 texels span the view width, v = 0 at the horizon. |
| `block16.dat` / `block32.dat` | 45,056 / 155,648 | Terrain texture atlases, 256 pixels wide: 176 rows of 16x16 tiles (16 per row) / 608 rows of 32x32 tiles (8 per row). Texture id = row*(256/B) + col; the renderer addresses texels as atlas[(v<<8)|u] from the tile's top-left. Only 148 ids are used; the game picks 32 when >= 2 MB are free. |
| `search.dat` | 1,024 | 32x32 ring numbers of the spiral searches (ring 0 = a 2x2 quad whose first cell is the centre); `spiral_search_init_101b0` builds the ring offset tables from it: per ring r a 6-byte record {ptr, u16 count} at 0xade28 + r*6 pointing at 4-byte cells {i8 dx, i8 dy, u8 r, 0} (round 6). |
| `building.dat` + `.tab` | 18,761 | *Corrected (round 5 merge): not sprites to draw* - the castle size table / footprint maps (68 entries, pointer at 0xadfb0): 1..7 the player castle levels (8x8, 21x21, 21x21, 35x35, 35x35, 48x48, 48x48), 8..16 1x1, 17..68 wizard buildings (size = THING_INIT.Parent + 0x10). Span-encoded like the sprites; each byte is a terrain instruction (ENGINE.md "Castle footprint maps" and the round-5 corrections). In 320x200 the tab relocation doubles w / h and the castle code halves them again. |
| `sndsX-Y.dat` + `.tab` | varies | Digital sound banks, see "Sound and music banks" below. |
| `musicX-Y.dat` + `.tab`, `inst.bnk`, `drum.bnk` | | HMI MIDI music (see below) + AdLib .BNK instrument banks for the internal OPL2 driver (HMI drivers `hmidrv.386`, `hmidet.386`). |
| `intro/*.dat` | 296,632 each | Bullfrog movie format (first bytes `0c 00 00 00 12 af 29 00 40 01 c8 00`: 320x200). *Corrected (round 6 merge): a FLIC variant with a 12-byte header, see "FLI movies" below; in this package six of the files are byte-identical placeholders.* |
| `*text.dat` | | Language text (e=English, f=French, g=German, i=Italian), plain. |
| `screens/*.dat` 64,000 | | Raw 320x200 8-bit screens with matching `.pal`. *Corrected (round 6 merge): except `globe.dat`, `scroll.dat` and `timer.dat`, which are FLI streams (see "FLI movies").* |

## Sound and music banks (`data/snds<set>-<q>`, `data/music<set>-<d>`) (round 5 merge, port_sound.md)

Both are a `.dat` + `.tab` pair, each RNC-compressed. The `.tab` is a list of 0x20-byte records:

| Offset | Size | Content |
|---|---|---|
| +0x00 | 18 | DOS file name of the source (`WAVES2-.RAW`, `CGAME1.HMP`), NUL padded |
| +0x12 | u32 | byte offset into the `.dat` (the game adds the `.dat` base) |
| +0x16 | 2 x u16 | 0 |
| +0x1a | u32 | length, a multiple of 16; the last 16 bytes are packer padding (the game plays length - 0x10) |
| +0x1e | u16 | 0x5a in sample records, 0 in record 0 (never read) |

Record 0 is a header (empty name, length = total size); the relocation loop counts the records after
it. **Samples**: index = game sound id (1..45 in set 0); 8-bit unsigned mono PCM. `<q>` is the memory
class DAT_0009e328: 1 = 22050 Hz bank (default with >= 5 MB free; 46 samples), 0 = the same sounds at
11025 Hz (45), 3 = low-memory 11025 Hz bank whose big loops (WAVES2, WHB03985) are NULL.RAW. Set 0 is
the in-game bank (game_main always loads it); sets 1..13 hold the speech of the cue scripts (front end,
intro / outro). **Music**: `<d>` = DAT_0012e06e from the HMI MIDI device id: 0 = OPL2 FM (`*.HMP`),
1 = MT-32 (`*.ROL`), 2 = General MIDI / AWE32 (`*.GEN`) - the file names in the records say so. Set 0 =
CGAME1..3 (the three level tracks; GameState+0x240 = `lcg(level) % 3 + 1`) + CSETUP (front end); set 1 =
CINTRO4..6. Each record is one HMI MIDI file ("HMIMIDIP" header).

## HMI MIDI songs (`.HMP`, `.GEN`, `.ROL` records of the music banks) (round 6 merge, port_audio.md, `src/mcdata/hmp.h`)

Each music bank record is one song in HMI's "HMIMIDIP" format (all three device variants; the device only changes
the instrument choices). Read by the HMI sequencer linked into carpet.exe (`snd_midi_init_song_5e53a`,
`snd_midi_song_setup_5ec41`, `snd_midi_read_varlen_5eedf`, the timer callback `snd_midi_song_timer_cb_66898`).

| Offset | Size | Content |
|---|---|---|
| +0x000 | 8 | "HMIMIDIP" (rest of the first 0x20 bytes zero) |
| +0x020 | u32 | file length (0 in the shipped files) |
| +0x030 | u32 | track (chunk) count, <= 32 |
| +0x034 | u32 | 120 (MIDI-style division; never read) |
| +0x038 | u32 | sequencer rate = timer events per second = song ticks per second (120 in every file) |
| +0x03c | u32 | song length in whole seconds (informational) |
| +0x040 | u32[16] | per MIDI channel priority (channel stealing; only with channel mapping on) |
| +0x080 | u32[32][5] | per track up to 5 HMI device ids it plays on (0 ends the list): 0xa000 = any General MIDI device (0xa000 / 0xa001 MPU-401 / 0xa008 AWE32), others must equal the driver id; an empty list plays everywhere. The shipped tracks list {0xa002, 0xa000} or nothing |
| +0x300 | u32, u16 | end-of-song far callback (filled at run time) |
| +0x308 | | the tracks back to back: u32 chunk number, u32 chunk length including this 12-byte header, u32 MIDI channel of the track (used by the reset at song end), then the events |

- **Delta**: little-endian groups of 7 bits; the **last** byte has bit 7 set (the reverse of standard MIDI).
- **No running status**: every event carries its status byte; its length comes from the status (tables 0x9fa0e /
  0x9fa1e): 8x / 9x / Bx / Ex 3 bytes, Ax / Cx / Dx 2 (Ax is 2 in the HMI table), F0 0, F1 1, F2 2, F3 1, F8..FF 2,
  except FF 2F (end of track) 3 and FF 51 (tempo, ignored) 5.
- **Timing**: every timer event increments each track's counter; an event fires when its delta <= the counter, the
  counter is then cleared and delta-0 events follow in the same tick. Absolute tick = sum of the deltas, + 1 when the
  track's first delta is 0. The song ends when every playing track has reached FF 2F; the sequencer resets the
  channels (CC7 = 0) and rewinds, it does not loop (the game restarts the track).
- **Events in the shipped songs**: 9x (note off = velocity 0), Bx (7 volume, 10 pan, 116 / 117 in CSETUP), Cx, Ex and
  FF 2F only.
- The OPL2 bank (`-0`) also needs `data/inst.bnk` / `data/drum.bnk` (AdLib instrument banks, loaded by
  `music_init_hmi_4d550` for device 0xa002); their layout is documented by the round-6 OPL work (port_opl.md).

## FLI movies (`intro/*.dat`, `data/screens/globe.dat` / `scroll.dat` / `timer.dat`, animated tmaps) (round 6 merge, port_fli.md)

- **Header**: 12 bytes {u32 12, u16 0xAF12, u16 frames, u16 width, u16 height} instead of Autodesk's 128;
  `fli_play_508f0` reads 12 bytes and continues at offset 12 whatever the size field says.
- **Frames**: standard 16-byte 0xF1FA frame chunks with sub-chunks {u32 size, u16 type}; the files hold `frames + 1`
  chunks (the last is the ring frame back to frame 0). `fli_play` shows frames 0 .. frames - 2 only.
- **Chunk types** decoded by `fli_decode_present_frame_50600`: 4 COLOR256 (6-bit values, no scaling), 7 SS2, 11
  COLOR64, 12 LC, 13 BLACK, 15 BRUN, 16 COPY, 18 PSTAMP. The shipped files use only 4, 7 and 15. The memory-stream
  player (`flic_play_chunk_50dfd`) decodes only 7 and 15.
- **Census** (all 320x200, palette in frame 0): intel / intro / levelw1 / levelw2 / levelose / outro.dat are
  byte-identical placeholders (41 frames, md5 43add94f...); logo 91 frames; title-01 150 (two palette frames); scroll 26;
  title-02 4. The cue scripts in the exe were written for the CD movies (the intro script runs to frame 2944).
- **`globe.dat`, `scroll.dat`, `timer.dat`**: their frame-chunk sizes do not match the sub-chunks (the SS2 sizes are
  wrong); only the memory-stream player, which continues where the SS2 data ends, plays them.
- **Cue scripts** (in the exe, not in the files): 7-byte records {u16 frame, char op, i16 arg, u16 unused}; ops in
  ENGINE.md ("Port round 5 corrections").

## Save games (`save\carpet%02X.gam`, slot 0..5) (round 5 merge)

Written by `fe_savegame_save_591d0`, read by `fe_savegame_load_59030`; raw (not RNC), 142 bytes:

| Offset | Size | Content |
|---|---|---|
| 0x00 | u32 | version 4 |
| 0x04 | 0x14 | slot name (default "Game One".."Game Six", pointers at 0x9e4e8) |
| 0x18 | 0x20 | Config+0x1d string |
| 0x38 | 0x20 | Config+0x3d string |
| 0x58 | 0xc | GameState+0x2195..0x21a0 (detail options) |
| 0x64 | u32 | check value `(Config.level + DAT_0012ed30 + DAT_0012ed31) * 4` |
| 0x68 | 0x18 | GameState+0x3bd6 (= PlayerRec[0]+0x7cb): campaign progress |
| 0x80 | u8 | DAT_0012ed30 |
| 0x81 | u8 | DAT_0012ed31 |
| 0x82 | 0xc | GameState+0x2195 again |

A save holds no game state: it resumes the campaign at a level start.

## Movies (`movie/gam%05d.dat`, `map%05d.dat`, `mvi%05d.dat`) (round 5 merge, port_thing.md, port_reference.md)

- `gam00000.dat` (232,707 bytes = 0x38d03): the raw GameState block, written by `demo_save_state_3c2c0`
  413 ticks after level start in the middle of `player_commands_process` (412 complete thing updates).
  The loader accepts RNC too; the game writes it raw. It holds raw pointers of the recording run;
  `demo_relink_state_pointers_3dc10` only rebases the descriptor pointers (player 0's thing =
  descriptor 7) and the players' own Things.
- `map00000.dat` (398,018 bytes = 0x612c2): `demo_save_terrain_3c430` layout: the four 64 KB byte maps
  (texture 0xCDFB0, height 0xDDFB0, light 0xEDFB0, flags 0xFDFB0), the 128 KB u16 cell -> thing heads
  (0x10DFB0) and `g_corner_tex_table` (0xb58b0, 2401 x 2 bytes = 0x12C2).
- `mvi00000.dat` (342,010 bytes): headerless stream of 10-byte command packets (ENGINE.md "Player
  command packet": +0 cmd, +1 arg, +2 second arg, +3 / +4 steer, +5 key bits), one per player per tick
  (4 players x 8550 ticks) plus the final quit packet.
- Movie 0 was recorded and plays in 320x200, on level 38, with the French notices loaded.
