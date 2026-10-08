/* Bullfrog span-encoded sprites (.dat + .tab). */
#ifndef MC_SPRITE_H
#define MC_SPRITE_H
#include <stdint.h>
#include <stddef.h>
#include "mcfile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    mc_blob       dat;      /* decompressed pixel container */
    mc_tab_entry *entries;  /* decompressed table */
    size_t        count;
} mc_sprite_set;

/* Load <game_dir>/<name>.dat + .tab (both may be RNC). */
int  mc_sprite_set_load(const char *game_dir, const char *name, mc_sprite_set *out);
void mc_sprite_set_free(mc_sprite_set *s);

/* Decode sprite `index` into `dst` (width*height bytes, 0 = transparent).
 * Returns bytes of source consumed, or 0 if the entry is empty/invalid. */
size_t mc_sprite_decode(const mc_sprite_set *s, size_t index, uint8_t *dst, size_t dst_cap);

/* Blit a decoded sprite into an 8-bit surface with colour-key 0. */
void mc_sprite_blit(const uint8_t *src, int w, int h, uint8_t *surface, int sw, int sh, int x, int y);

#ifdef __cplusplus
}
#endif
#endif
