// tables_test: regenerates the tables.dat image with tables_load_or_generate(force) for both
// texture block sizes and compares it region by region with the shipped data/tables.dat.
// Exit 0 when the shade, blend and circle regions match byte for byte (they do not depend on the
// textures); the texture-average region is reported per block size. Also checks the texture table
// geometry, the UV rescale and that palette / sky load. Bonus: dumps PPMs of the block32 atlas and
// of the shade / blend tables (through the palette) next to the executable.
#define _CRT_SECURE_NO_WARNINGS
#include "mc_globals.h"
#include "mc_math.h"
#include "tables.h"
#include "../mcdata/mcfile.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("CHECK FAILED: %s (line %d)\n", #cond, __LINE__); g_failures++; } } while (0)

static size_t count_mismatch(const uint8_t *a, const uint8_t *b, size_t len) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++) n += a[i] != b[i];
    return n;
}

static void describe_region(const char *name, const uint8_t *p, size_t len) {
    size_t nonzero = 0; unsigned mn = 255, mx = 0;
    for (size_t i = 0; i < len; i++) { nonzero += p[i] != 0; if (p[i] < mn) mn = p[i]; if (p[i] > mx) mx = p[i]; }
    std::printf("  shipped %s: %zu bytes, %zu non-zero, min %u max %u, first 16:", name, len, nonzero, mn, mx);
    for (size_t i = 0; i < 16 && i < len; i++) std::printf(" %02x", p[i]);
    std::printf("\n");
}

static void write_ppm(const std::string &path, const uint8_t *idx, int w, int h, size_t stride) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  (could not write %s)\n", path.c_str()); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *c = g_palette6 + idx[y * stride + x] * 3;
            const uint8_t rgb[3] = { (uint8_t)((c[0] << 2) | (c[0] >> 4)), (uint8_t)((c[1] << 2) | (c[1] >> 4)), (uint8_t)((c[2] << 2) | (c[2] >> 4)) };
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    std::printf("  wrote %s (%dx%d)\n", path.c_str(), w, h);
}

int main(int argc, char **argv) {
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    mc_globals_init();

    CHECK(tables_load_palette(game_dir));
    CHECK(tables_load_sky(game_dir));
    {
        size_t nz = 0;
        for (int i = 0; i < 768; i++) { nz += g_palette6[i] != 0; CHECK(g_palette6[i] < 64); }
        CHECK(nz > 0);
        nz = 0;
        for (int i = 0; i < 65536; i++) nz += g_sky[i] != 0;
        CHECK(nz > 0);
        std::printf("palette entry 0 = %u %u %u, entry 255 = %u %u %u\n", g_palette6[0], g_palette6[1], g_palette6[2],
                    g_palette6[0x2fd], g_palette6[0x2fe], g_palette6[0x2ff]);
    }

    // Shipped tables.dat, loaded independently.
    char path[1024];
    mc_path_join(path, sizeof path, game_dir, "data/tables.dat");
    mc_blob ship;
    if (!mc_read_unpacked(path, &ship) || ship.len != MC_TABLES_SIZE) {
        std::printf("cannot read %s (len %zu)\n", path, ship.len);
        return 2;
    }
    // The plain loader must produce the shipped bytes.
    CHECK(tables_load_or_generate(game_dir, false));
    CHECK(count_mismatch(g_tables_image, ship.data, MC_TABLES_SIZE) == 0);

    bool core_ok = true;
    size_t avg_mismatch[2] = { 0, 0 };
    const int sizes[2] = { 16, 32 };
    for (int s = 0; s < 2; s++) {
        const int B = sizes[s];
        g_state->texture_block_size = (uint16_t)B;
        CHECK(tables_load_textures(game_dir));
        std::printf("\n== block size %d: atlas %zu bytes = %zu pixel rows ==\n", B, g_texture_atlas_size, g_texture_atlas_size / 256);
        const int cols = 256 / B;
        CHECK(g_texture_table[1] - g_texture_table[0] == B);
        CHECK(g_texture_table[cols] - g_texture_table[0] == B * 256);
        CHECK(g_texture_table[0] == g_texture_atlas);
        CHECK(g_texture_table[255] - g_texture_table[0] == (256 / cols - 1) * B * 256 + (cols - 1) * B);
        for (int i = 0; i < 256; i++) CHECK(g_texture_table[i] - g_texture_atlas == (i / cols) * B * 256 + (i % cols) * B);
        {
            int past_end = 0;
            for (int i = 0; i < 256; i++) past_end += (size_t)(g_texture_table[i] - g_texture_atlas) >= g_texture_atlas_size;
            std::printf("  texture ids pointing past the end of the file: %d (first id %d)\n", past_end, 256 - past_end);
        }
        {
            int zeros = 0, scaled = 0;
            for (int i = 0; i < 256; i++) {
                if (g_uv_table_data[i] == 0) { zeros++; CHECK(g_uv_table[i] == 0); }
                else { scaled++; CHECK(g_uv_table[i] == (int32_t)((B << 16) - 1)); }
            }
            std::printf("  uv table: %d zero entries kept, %d entries = 0x%x\n", zeros, scaled, (B << 16) - 1);
        }

        CHECK(tables_load_or_generate(game_dir, true));
        const uint8_t *gen = g_tables_image, *ref = ship.data;

        size_t shade_total = 0;
        for (int row = 0; row < 0x40; row++) {
            const size_t m = count_mismatch(gen + (row << 8), ref + (row << 8), 256);
            shade_total += m;
            if (m) std::printf("  shade row 0x%02x: %zu mismatches\n", row, m);
        }
        const size_t blend_m = count_mismatch(gen + 0x4000, ref + 0x4000, 0x10000);
        const size_t avg_m = count_mismatch(gen + 0x14000, ref + 0x14000, 0x100);
        const size_t copy_m = count_mismatch(gen + 0x14100, ref + 0x14100, 0x80);
        const size_t gap1_m = count_mismatch(gen + 0x14180, ref + 0x14180, 0x180);
        const size_t circle_m = count_mismatch(gen + 0x14300, ref + 0x14300, 0x100);
        const size_t gap2_m = count_mismatch(gen + 0x14400, ref + 0x14400, 0x200);
        std::printf("  shade rows 0x00..0x3f: %zu mismatches total\n", shade_total);
        std::printf("  blend table (+0x4000, 65536 bytes): %zu mismatches\n", blend_m);
        std::printf("  texture averages (+0x14000..+0x14100): %zu mismatches\n", avg_m);
        std::printf("  +0x80 copy of textures 0x80..0xff (+0x14100..+0x14180): %zu mismatches\n", copy_m);
        std::printf("  gap +0x14180..+0x14300: %zu mismatches vs zero\n", gap1_m);
        std::printf("  circle profile (+0x14300): %zu mismatches\n", circle_m);
        std::printf("  gap +0x14400..+0x14600: %zu mismatches vs zero\n", gap2_m);
        if (avg_m) {
            std::printf("  first differing texture ids:");
            int shown = 0;
            for (int i = 0; i < 256 && shown < 12; i++)
                if (gen[0x14000 + i] != ref[0x14000 + i]) { std::printf(" %d(gen %u ship %u)", i, gen[0x14000 + i], ref[0x14000 + i]); shown++; }
            std::printf("\n");
        }
        avg_mismatch[s] = avg_m;
        if (shade_total || blend_m || circle_m) core_ok = false;

        if (s == 1) {
            std::string dir = argv[0];
            const size_t slash = dir.find_last_of("/\\");
            dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
            write_ppm(dir + "/block32_atlas.ppm", g_texture_atlas, 256, (int)(g_texture_atlas_size / 256), 256);
            write_ppm(dir + "/shade_table.ppm", g_shade_table(), 256, 64, 256);
            write_ppm(dir + "/blend_table.ppm", g_blend_table(), 256, 256, 256);
        }
    }

    std::printf("\n== shipped tables.dat extras ==\n");
    {
        // Is the shipped +0x80 copy consistent with out[t] / out[t+0x80] semantics?
        size_t copy_lo = 0;
        for (int t = 0; t < 0x80; t++) copy_lo += ship.data[0x14000 + t + 0x80] != ship.data[0x14000 + t];
        std::printf("  (shipped [0x14080+t] != [0x14000+t] for t<0x80: %zu, expected non-zero since ids 0x80.. overwrite)\n", copy_lo);
        size_t copy_hi = 0;
        for (int t = 0x80; t < 0x100; t++) copy_hi += ship.data[0x14000 + t + 0x80] != ship.data[0x14000 + t];
        std::printf("  shipped [0x14100+t-0x80] vs [0x14000+t] for t in 0x80..0xff: %zu differences (0 = +0x80 copy confirmed)\n", copy_hi);
        describe_region("gap +0x14180..+0x14300", ship.data + 0x14180, 0x180);
        describe_region("gap +0x14400..+0x14600", ship.data + 0x14400, 0x200);
        std::printf("  shipped circle[0..4] = %02x %02x %02x %02x %02x, [255] = %02x\n", ship.data[0x14300], ship.data[0x14301],
                    ship.data[0x14302], ship.data[0x14303], ship.data[0x14304], ship.data[0x143ff]);
    }

    std::printf("\n== summary ==\n");
    std::printf("  shade + blend + circle byte-exact: %s\n", core_ok ? "yes" : "NO");
    std::printf("  texture averages: block16 %zu mismatches, block32 %zu mismatches -> shipped file generated with %s\n",
                avg_mismatch[0], avg_mismatch[1],
                avg_mismatch[0] == 0 ? "block16" : avg_mismatch[1] == 0 ? "block32" : "neither (see above)");
    std::printf("  other checks failed: %d\n", g_failures);
    mc_blob_free(&ship);
    mc_globals_shutdown();
    return (core_ok && g_failures == 0) ? 0 : 1;
}
