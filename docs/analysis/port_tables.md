# Tables and textures port (tables.cpp) - agent report, 2026-10-06

Files: `src/mcengine/tables.cpp`, `tables.h` (added `render_set_texture_uv_scale(int B)`),
`src/tests/tables_test.cpp` + `.cmake`. Test dumps: `block32_atlas.ppm/.png` (256x608),
`shade_table.ppm/.png` (256x64), `blend_table.ppm/.png` (256x256) next to the exe.

## Functions translated

| Port | Original |
|---|---|
| `render_set_texture_uv_scale(int B)` | render_set_texture_uv_scale_284f0: every non-zero dword of the 32x8 UV table -> `(B<<16)-1` |
| `tables_load_palette` | palette.dat load into DAT_000adf90 (768 x 6-bit, RNC) |
| `tables_load_textures` | first part of tables_load_or_generate_3eaa0 (0x3eaa0..0x3eb51) + the block16/32.dat allocation of mem_init_pools_59500 |
| `tables_load_or_generate(game_dir, force)` | second part of 3eaa0 (0x3eb51..0x3ed1d) |
| `palette_build_tint_table(pal, out, r, g, b, fr, fg, fb)` | palette_build_tint_table_4cb88 |
| `texture_average_colours(pal, out)` | texture_average_colours_72147 |
| `tables_load_sky` | sky.dat -> g_sky |

Stack-argument order of 4cb88 (pushes at 0x3ebc5..0x3ebec): `pal, out, r, g, b, fr, fg, fb` (factors
used as 16-bit words). 72147: `pal, out`.

## The three tint loops (0x3eb84..0x3ecbf)

1. shade rows 0x00..0x1f: `out = 0xb99b0 + (row<<8)`; target r,g,b = palette entry 255 (43,46,63);
   all factors `= 0x100 - 8*row`. Row 0 = everything mapped to colour 255's nearest.
2. shade rows 0x20..0x3f: target = palette entry 0 (black); factors `= 8*(row - 0x20)`. Row 0x20 =
   identity, row 0x3f = 0xf8/256 toward black.
3. blend rows a = 0..0xff: `out = 0xbd9b0 + (a<<8)`; target = entry a; all factors 0x55. So
   `blend[a<<8|b] = nearest(pal[b] + (pal[a]-pal[b])*85/256)`: one third toward a (the asymmetry).

Circle profile: `circle[i] = (u8)isqrt(0x10000 - i*i)` for i = 0..255, then `circle[0] = 0xff`.

Integer semantics: target channel = `pal + (uint8)(((int16)((int8)(rgb - pal) * factor16)) >> 8)`;
distance = `2*dr^2 + 2*dg^2 + db^2` with 8-bit signed differences summed in 16 bits, strict `<` so the
first minimum wins. The averager sums 6-bit components of a fixed 32x32 window at stride 256 (also for
block16), `>> 10`, nearest by `dr^2+dg^2+db^2` in 16 bits; writes `out[t]` and `out[t+0x80]`.

## Verification (tables_test, exit 0)

- Plain load reproduces the shipped file (0 mismatches over 0x14600 bytes).
- Forced regeneration, both block sizes: shade rows 0..0x3f **0 mismatches**, blend **0**, circle **0**.
  Texture averages: block16 236 / block32 121 mismatches (block32 ids 0..14 and 23..27 match, so the
  shipped file used the block32 layout but an earlier texture set; the game only regenerates when the
  file is missing and the averages only feed the overview map / radar colours). 104 block32 ids
  (152..255) point past the end of block32.dat and read pool garbage in the original.
- Texture table geometry verified (`[1]-[0] == B`, `[256/B]-[0] == B*256`); UV table 128 zero entries
  kept, 128 rewritten. Palette and sky load.

## tables.dat gap regions

- +0x14180..+0x14300: all zero. +0x14400..+0x14600: 16 non-zero bytes
  `ff 0b 35 75 55 b3 0a b2 54 74 94 93 5d 92 91 90` then zeros - leftover memory from the build
  machine, nothing writes there. Shipped circle profile `[0..4] = ff`, `[255] = 0x16`.

## Deviations / open questions

- Atlas padding instead of clamping: the port allocates `rows*B*256 + 32*256` zeroed bytes and copies
  the file in, so all 256 pointers stay valid with the original geometry (past-the-end textures read
  colour 0). `g_texture_atlas_size` reports the unpacked file length.
- The port never saves tables.dat and has no sticky flag DAT_0009437c; the image is cleared before
  generating.
- Texture-average mismatch unresolved (stale shipped file is the likely cause).

## Corrections to ENGINE.md / FORMATS.md

- Shade table has 64 generated rows: 0..0x1f toward palette entry 255 (fog), 0x20 identity,
  0x21..0x3f toward black. The "33 levels" are 0..0x20.
- blend[a<<8|b] = one third toward a.
- texture_average_colours_72147 confirmed as the producer of 0xCD9B0 (+ copy at +0x80); always a 32x32
  window at stride 256.
- FORMATS.md: block32.dat = 608 rows x 256 (not 152 x 1024); block16.dat = 176 x 256; both 256-wide
  atlases of BxB tiles, texture id = row*(256/B) + col. tables.dat layout as above.
