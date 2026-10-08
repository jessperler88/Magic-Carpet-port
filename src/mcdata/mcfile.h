/* File helpers: read whole files, transparently RNC-decompress, .tab tables. */
#ifndef MC_FILE_H
#define MC_FILE_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *data;
    size_t   len;
} mc_blob;

/* Read a file verbatim. Returns 0 on failure (blob->data NULL). */
int  mc_read_file(const char *path, mc_blob *out);
/* Read a file and RNC-decompress it if it is compressed (handles the 8-byte
 * "BULLFROG" tag used by tmaps.dat). */
int  mc_read_unpacked(const char *path, mc_blob *out);
void mc_blob_free(mc_blob *b);

/* file_load_rnc_3cbe0 equivalent: read <path>, RNC-decompress into `dest` (capacity `cap`) or copy
 * verbatim when the file is not compressed. Returns the number of bytes stored, 0 if the file is
 * missing, -1 if it does not fit, -2 on a decompression error. */
long mc_load_rnc_into(const char *path, uint8_t *dest, size_t cap);

/* Join game_dir + relative path (forward or back slashes) into buf. */
void mc_path_join(char *buf, size_t cap, const char *game_dir, const char *rel);

/* Sprite .tab entry: u32 offset, u8 width, u8 height. */
typedef struct { uint32_t offset; uint8_t width, height; } mc_tab_entry;
/* Parse a (decompressed) .tab blob. Returns entry count; *entries points into
 * a malloc'd array the caller frees. */
size_t mc_tab_parse(const mc_blob *tab, mc_tab_entry **entries);

/* VGA palette: 256 x 6-bit RGB -> 8-bit RGB triples (768 bytes out). */
void mc_palette_to_rgb(const uint8_t *pal6, uint8_t *rgb8);

#ifdef __cplusplus
}
#endif
#endif
