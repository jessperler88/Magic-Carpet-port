// Data tables and textures: tables.dat load/regenerate (tables_load_or_generate_3eaa0), texture
// atlas (block16/32.dat), palette, sky. Owner: tables.cpp.
#pragma once
#include <cstdint>

// Loads data/palette.dat into g_palette6. Returns false when the file is missing.
bool tables_load_palette(const char *game_dir);

// Loads data/block16.dat or block32.dat according to g_state->texture_block_size into
// g_texture_atlas and fills g_texture_table (atlas + row*B*256 + col*B) exactly like the first part
// of tables_load_or_generate_3eaa0; also rescales g_uv_table (render_set_texture_uv_scale_284f0).
bool tables_load_textures(const char *game_dir);

// Loads data/tables.dat into g_tables_image, or regenerates it from the palette and textures when
// the file is missing (texture_average_colours_72147, palette_build_tint_table_4cb88 x3, circle
// profile). `force_generate` runs the generator even if the file exists (used by the test that
// compares the generated image with the shipped file).
bool tables_load_or_generate(const char *game_dir, bool force_generate = false);

// Loads data/sky.dat (256x256) into g_sky.
bool tables_load_sky(const char *game_dir);

// render_set_texture_uv_scale_284f0: rewrites every non-zero entry of g_uv_table to (B<<16)-1.
void render_set_texture_uv_scale(int B);

// The generator pieces, exposed for tests. palette_build_tint_table_4cb88 takes the stack args
// (pal, out, r, g, b, fr, fg, fb); texture_average_colours_72147 takes (pal, out) and reads
// g_texture_table (needs tables_load_textures first).
void palette_build_tint_table(const uint8_t *pal6, uint8_t *out256, int r, int g, int b, int fr, int fg, int fb);
void texture_average_colours(const uint8_t *pal6, uint8_t *out);
