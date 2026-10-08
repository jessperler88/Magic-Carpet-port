/* data/tmaps.dat + tmaps.tab: 528 RNC chunks of billboard/texture images. */
#ifndef MC_TMAPS_H
#define MC_TMAPS_H
#include <stdint.h>
#include <stddef.h>
#include "mcfile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t unpacked_size;
    uint32_t offset;
    uint16_t group;
} mc_tmap_entry;

typedef struct {
    uint8_t  kind;      /* 2 = plain 8-bit image, 3 = image + extra data */
    uint8_t  unk;
    uint16_t width, height;
    uint8_t *pixels;    /* width*height, inside `raw` */
    uint8_t *raw;       /* whole decompressed chunk (malloc'd) */
    size_t   raw_len;
} mc_tmap;

typedef struct {
    mc_blob        dat;      /* raw (still compressed) tmaps.dat */
    mc_tmap_entry *entries;
    size_t         count;
} mc_tmap_set;

int  mc_tmap_set_load(const char *game_dir, mc_tmap_set *out);
void mc_tmap_set_free(mc_tmap_set *s);
/* Decompress chunk `index`. Returns 0 on failure. Free with mc_tmap_free. */
int  mc_tmap_get(const mc_tmap_set *s, size_t index, mc_tmap *out);
void mc_tmap_free(mc_tmap *t);

#ifdef __cplusplus
}
#endif
#endif
