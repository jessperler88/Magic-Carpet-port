/* Smoke tests for the C data layer against the retail files.
 * usage: mcdata_tests <game_dir>  */
#include "level.h"
#include "mcfile.h"
#include "rnc.h"
#include "sprite.h"
#include "tmaps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : "../MagicCarpet/magic";
    char path[1024];

    /* RNC: palette.dat is 765 packed -> 768 unpacked */
    mc_blob pal;
    mc_path_join(path, sizeof path, game, "data/palette.dat");
    CHECK(mc_read_unpacked(path, &pal), "read palette.dat");
    CHECK(pal.len == 768, "palette len %zu", pal.len);
    mc_blob_free(&pal);

    /* every level loads and has the expected shape */
    int loaded = 0, things = 0;
    for (int i = 0; i < 70; i++) {
        mc_level lv;
        if (mc_level_load(game, i, &lv)) { loaded++; things += mc_level_active_things(&lv); }
    }
    CHECK(loaded == 70, "levels loaded %d", loaded);
    mc_level lv0;
    CHECK(mc_level_load(game, 0, &lv0), "level 0");
    CHECK(lv0.gen.seed == 1921 && lv0.gen.off == 41339 && lv0.gen.raise == 2834, "level 0 header %d %d %d", lv0.gen.seed, lv0.gen.off, lv0.gen.raise);
    CHECK(mc_level_active_things(&lv0) == 591, "level 0 things %d", mc_level_active_things(&lv0));
    /* round trip */
    uint8_t buf[MC_LEVEL_SIZE];
    mc_level_write(&lv0, buf);
    mc_level lv0b;
    CHECK(mc_level_parse(buf, sizeof buf, &lv0b) && memcmp(&lv0, &lv0b, sizeof lv0) == 0, "level round trip");

    /* sprites */
    mc_sprite_set hud;
    CHECK(mc_sprite_set_load(game, "data/hspr0-0", &hud), "load hspr0-0");
    CHECK(hud.count == 87, "hspr count %zu", hud.count);
    uint8_t px[256 * 256];
    size_t used = mc_sprite_decode(&hud, 1, px, sizeof px);
    CHECK(hud.entries[1].width == 64 && hud.entries[1].height == 44, "sprite 1 dims");
    CHECK(used == 2904, "sprite 1 consumed %zu", used);
    mc_sprite_set_free(&hud);

    /* tmaps */
    mc_tmap_set tm;
    CHECK(mc_tmap_set_load(game, &tm), "load tmaps");
    CHECK(tm.count == 529, "tmap count %zu", tm.count);
    int ok = 0, kinds2 = 0, kinds3 = 0;
    for (size_t i = 0; i < tm.count; i++) {
        mc_tmap t;
        if (mc_tmap_get(&tm, i, &t)) { ok++; if (t.kind == 2) kinds2++; else if (t.kind == 3) kinds3++; mc_tmap_free(&t); }
    }
    CHECK(ok == 529 && kinds2 == 316 && kinds3 == 213, "tmaps decoded %d (k2=%d k3=%d)", ok, kinds2, kinds3);
    mc_tmap_set_free(&tm);

    printf("%s: %d failure(s), %d levels, %d things total\n", fails ? "FAILED" : "OK", fails, loaded, things);
    return fails ? 1 : 0;
}
