// Data tables and textures of carpet.exe: tables.dat load / regenerate, texture atlas and texture
// pointer table, palette, sky. Translated from tables_load_or_generate_3eaa0 (0x3eaa0..0x3ed1d),
// palette_build_tint_table_4cb88, texture_average_colours_72147, render_set_texture_uv_scale_284f0.
#include "tables.h"
#include "mc_globals.h"
#include "mc_math.h"
#include "../mcdata/mcfile.h"
#include <cstdlib>
#include <cstring>

// render_set_texture_uv_scale_284f0: every non-zero dword of the 32 x 8 UV table DAT_00093b48
// becomes (B << 16) - 1, i.e. the last texel of a B-texel texture in 16.16.
void render_set_texture_uv_scale(int B) {
    const int32_t v = (int32_t)(((uint32_t)B << 16) - 1u);
    for (int i = 0; i < 256; i++)
        if (g_uv_table[i] != 0) g_uv_table[i] = v;
}

// tables_load_palette: data/palette.dat (RNC) -> g_palette6 (768 x 6-bit). The exe keeps the
// pointer in DAT_000adf90.
bool tables_load_palette(const char *game_dir) {
    char path[1024];
    mc_path_join(path, sizeof path, game_dir, "data/palette.dat");
    const long n = mc_load_rnc_into(path, g_palette6, sizeof g_palette6);
    return n == (long)sizeof g_palette6;
}

// First part of tables_load_or_generate_3eaa0 (0x3eaa0..0x3eb51) plus the block file load that
// mem_init (0x59xxx) does right before calling it: the atlas DAT_000adf5c is block16.dat or
// block32.dat (B = g_state->texture_block_size), 256 pixels wide; texture id t = row * cols + col
// points at atlas + row * B * 256 + col * B with cols = 256 / B and rows = 256 / cols.
// The original allocates exactly the file (176 or 608 pixel rows), so ids whose block row lies past
// the end of the file (>= 176 for block16, >= 152 for block32) point at whatever followed the
// buffer in the memory pool. The port pads the atlas with zeros to the full rows * B * 256 bytes
// (+ 32 rows of slack for texture_average_colours' fixed 32 x 32 window) so every pointer in
// g_texture_table is valid and those textures read as colour 0.
bool tables_load_textures(const char *game_dir) {
    const int B = (int8_t)g_state->texture_block_size;      // movsx eax, byte [g_state + 0x21a0]
    const char *rel = (B == 16) ? "data/block16.dat" : (B == 32) ? "data/block32.dat" : nullptr;
    if (!rel) return false;

    char path[1024];
    mc_path_join(path, sizeof path, game_dir, rel);
    mc_blob blob;
    if (!mc_read_unpacked(path, &blob)) return false;

    const uint32_t cols = 256u / (uint32_t)B;               // idiv then div in the original
    const int32_t rows = (int32_t)(256u / cols);
    const size_t geom = (size_t)rows * (size_t)B * 256u + 32u * 256u;
    const size_t alloc = blob.len > geom ? blob.len : geom;

    std::free(g_texture_atlas);
    g_texture_atlas = static_cast<uint8_t *>(std::calloc(1, alloc));
    if (!g_texture_atlas) { mc_blob_free(&blob); g_texture_atlas_size = 0; return false; }
    std::memcpy(g_texture_atlas, blob.data, blob.len);
    g_texture_atlas_size = blob.len;
    mc_blob_free(&blob);

    render_set_texture_uv_scale(B);

    // 0x3eaea..0x3eb4f: row loop (signed, jl) around the column loop (unsigned, jb).
    const uint8_t **tab = g_texture_table;
    int32_t row_base = 0;
    for (int32_t row = 0; row < rows; row++) {
        int32_t col_off = 0;
        for (uint32_t col = 0; col < cols; col++) {
            *tab++ = g_texture_atlas + col_off + row_base;
            col_off += B;
        }
        row_base += B * 0x100;
    }
    return true;
}

// palette_build_tint_table_4cb88 (stack args pal, out, r, g, b, fr, fg, fb; 16-bit factors).
// For each palette entry i: target_c = pal[i].c + hi8((int8)(c - pal[i].c) * factor_c) (8-bit
// adds, 16-bit signed product, high byte taken), then out[i] = first palette index j minimising
// 2*dr^2 + 2*dg^2 + db^2 with 8-bit signed differences and 16-bit sums (strict less, start 0x7fff).
void palette_build_tint_table(const uint8_t *pal6, uint8_t *out256, int r, int g, int b, int fr, int fg, int fb) {
    const int16_t f16[3] = { (int16_t)fr, (int16_t)fg, (int16_t)fb };
    const uint8_t rgb[3] = { (uint8_t)r, (uint8_t)g, (uint8_t)b };
    for (int i = 0; i < 256; i++) {
        const uint8_t *p = pal6 + i * 3;
        uint8_t target[3];
        for (int c = 0; c < 3; c++) {
            const int8_t d = (int8_t)(uint8_t)(rgb[c] - p[c]);          // sub al, bl ; movsx ax, al
            const uint16_t prod = (uint16_t)((int16_t)d * f16[c]);       // imul word [factor] -> ax
            target[c] = (uint8_t)(p[c] + (uint8_t)(prod >> 8));          // add bl, ah
        }
        int16_t best = 0x7fff;
        uint8_t best_i = 0;
        const uint8_t *q = pal6;
        for (int j = 0; j < 256; j++, q += 3) {
            const int8_t dr = (int8_t)(uint8_t)(q[0] - target[0]);
            const int8_t dg = (int8_t)(uint8_t)(q[1] - target[1]);
            const int8_t db = (int8_t)(uint8_t)(q[2] - target[2]);
            uint16_t dist = (uint16_t)((int16_t)dr * dr);                 // imul al -> ax
            dist = (uint16_t)(dist + dist);                              // add ax, ax
            const uint16_t t = (uint16_t)((int16_t)dg * dg);
            dist = (uint16_t)(dist + (uint16_t)(t + t));
            dist = (uint16_t)(dist + (uint16_t)((int16_t)db * db));
            if ((int16_t)dist < best) { best = (int16_t)dist; best_i = (uint8_t)j; }
        }
        out256[i] = best_i;
    }
}

// texture_average_colours_72147 (stack args pal, out). For each of the 256 texture pointers in
// DAT_0009afec: sum the 6-bit palette components of a fixed 32 x 32 window at stride 256 (even
// for block16), average (>> 10), pick the first palette entry minimising dr^2 + dg^2 + db^2 in
// 16-bit arithmetic, write out[t] and out[t + 0x80] (so out[0x100..0x17f] = textures 0x80..0xff).
void texture_average_colours(const uint8_t *pal6, uint8_t *out) {
    for (int t = 0; t < 256; t++) {
        const uint8_t *tex = g_texture_table[t];
        uint32_t sr = 0, sg = 0, sb = 0;
        if (tex) {
            for (int y = 0; y < 32; y++) {
                const uint8_t *row = tex + y * 256;
                for (int x = 0; x < 32; x++) {
                    const uint8_t *c = pal6 + (uint32_t)row[x] * 3;
                    sr += c[0]; sg += c[1]; sb += c[2];
                }
            }
        }
        const uint16_t ar = (uint16_t)(sr >> 10), ag = (uint16_t)(sg >> 10), ab = (uint16_t)(sb >> 10);
        int16_t best = 0x7fff;
        uint8_t best_i = 0;
        const uint8_t *q = pal6;
        for (int j = 0; j < 256; j++, q += 3) {
            const uint16_t dr = (uint16_t)(q[0] - ar);
            const uint16_t dg = (uint16_t)(q[1] - ag);
            const uint16_t db = (uint16_t)(q[2] - ab);
            uint16_t dist = (uint16_t)(dr * dr);                         // imul ax, ax (16-bit)
            dist = (uint16_t)(dist + (uint16_t)(dg * dg));
            dist = (uint16_t)(dist + (uint16_t)(db * db));
            if ((int16_t)dist < best) { best = (int16_t)dist; best_i = (uint8_t)j; }
        }
        out[t] = best_i;
        out[t + 0x80] = best_i;
    }
}

// tables_load_or_generate_3eaa0, second part (0x3eb51..0x3ed1d): load data/tables.dat into the
// 0x14600-byte image at 0xb99b0, else generate it. The generator (pushes at 0x3eb84..0x3ecbf):
//   texture_average_colours(pal, image + 0x14000)
//   rows 0x00..0x1f of the shade table: tint toward palette entry 255 (pal[0x2fd..0x2ff]),
//       factor 0x100 - 8*row for all three channels (row 0 = all colour 255, row 0x1f = 8/256)
//   rows 0x20..0x3f: tint toward palette entry 0 (pal[0..2]), factor 8*(row - 0x20)
//       (row 0x20 = identity, row 0x3f = 0xf8/256)
//   blend rows a = 0..0xff at image + 0x4000: tint toward palette entry a, factor 0x55 (85/256)
//   circle profile at image + 0x14300: [i] = (u8)isqrt(0x10000 - i*i), then [0] = 0xff
// then the original saves the image as data/tables.dat (the port does not).
bool tables_load_or_generate(const char *game_dir, bool force_generate) {
    if (!force_generate) {
        char path[1024];
        mc_path_join(path, sizeof path, game_dir, "data/tables.dat");
        if (mc_load_rnc_into(path, g_tables_image, MC_TABLES_SIZE) > 0) return true;
    }
    // The original generates into BSS (zero on first run); clear so the unused gaps are deterministic.
    std::memset(g_tables_image, 0, MC_TABLES_SIZE);
    const uint8_t *pal = g_palette6;

    texture_average_colours(pal, g_tex_avg_colour());

    for (int row = 0; row < 0x20; row++) {                         // 0x3ebc0..0x3ebfd, esi = 0x100 - 8*row
        const int f = 0x100 - 8 * row;
        palette_build_tint_table(pal, g_shade_table() + (row << 8), pal[0x2fd], pal[0x2fe], pal[0x2ff], f, f, f);
    }
    for (int row = 0x20; row < 0x40; row++) {                      // 0x3ec30..0x3ec6c, esi = 8*(row-0x20)
        const int f = 8 * (row - 0x20);
        palette_build_tint_table(pal, g_shade_table() + (row << 8), pal[0], pal[1], pal[2], f, f, f);
    }
    for (int a = 0; a < 0x100; a++) {                              // 0x3ec80..0x3ecbf, edi = 0x55
        palette_build_tint_table(pal, g_blend_table() + (a << 8), pal[a * 3], pal[a * 3 + 1], pal[a * 3 + 2], 0x55, 0x55, 0x55);
    }
    uint8_t *circle = g_circle_profile();                         // 0x3ecd0..0x3ecf6
    for (uint32_t i = 0; i < 0x100; i++)
        circle[i] = (uint8_t)mc_isqrt(0x10000u - i * i);
    circle[0] = 0xff;
    return true;
}

// tables_load_sky: data/sky.dat (RNC, 256 x 256) -> g_sky.
bool tables_load_sky(const char *game_dir) {
    char path[1024];
    mc_path_join(path, sizeof path, game_dir, "data/sky.dat");
    return mc_load_rnc_into(path, g_sky, sizeof g_sky) == (long)sizeof g_sky;
}
