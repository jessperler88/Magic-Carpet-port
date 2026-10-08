/* Digital sample banks data/snds<set>-<quality>.dat + .tab (and the music banks music<set>-<n>,
 * same container): RNC-compressed .dat with the raw sample data and an RNC-compressed .tab of
 * 0x20-byte records. Layout from sound_load_bank_5c990 / sound_bank_relocate_5ca58 and the
 * hmi_start_sample_64e7c call sites (sound_start_sample_4f8a0, sound_play_sample_5cbb0):
 *
 *   +0x00 char[18]  DOS file name of the source ("WAVES2-.RAW", NUL padded)
 *   +0x12 u32       byte offset into the .dat (the game adds the .dat base to make a pointer)
 *   +0x16 u16       0
 *   +0x18 u16       0
 *   +0x1a u32       byte length, a multiple of 16; the last 16 bytes are packer padding (the game
 *                   plays length - 0x10 bytes)
 *   +0x1e u16       0x5a in every sample record, 0 in record 0 (meaning unknown; never read)
 *
 * Record 0 is the bank header (empty name, length = total size); the relocation loop starts at
 * record 1 and counts the records after it into the sample count (DAT_0012e246). Sample index ==
 * game sound id (1..45 in set 0). The data is 8-bit unsigned mono for every bank the game ships:
 * quality 1 (>= 5 MB free, the default) = 22050 Hz, quality 0 and 3 = 11025 Hz
 * (sound_digital_init_4da10 programs the HMI driver rate from the quality class; the -3 bank is the
 * low-memory set with the local-player loops replaced by NULL.RAW). */
#ifndef MC_SNDBANK_H
#define MC_SNDBANK_H
#include <stddef.h>
#include <stdint.h>
#include "mcfile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MC_SNDBANK_RECORD_SIZE 0x20
#define MC_SNDBANK_PADDING     0x10

typedef struct {
    char     name[19];      /* NUL-terminated copy of +0x00 */
    uint32_t offset;        /* +0x12 */
    uint32_t length;        /* +0x1a raw length incl. the 16 bytes of padding */
    uint16_t unk1e;         /* +0x1e */
} mc_sndbank_record;

typedef struct mc_sndbank {
    mc_blob            dat;         /* decompressed sample data */
    mc_sndbank_record *records;     /* all records incl. record 0 */
    size_t             record_count;
    int                set;         /* which snds<set> / music<set> */
    int                quality;     /* the -<n> suffix (DAT_0009e328 for snds) */
    int                rate;        /* playback rate in Hz for snds banks (22050 / 11025), 0 for music */
} mc_sndbank;

/* Load <game_dir>/data/snds<set>-<quality>.dat + .tab. Returns 0 on failure (bank zeroed). */
int  mc_sndbank_load(const char *game_dir, int set, int quality, mc_sndbank *out);
/* Load any <game_dir>/data/<prefix><set>-<n>.dat/.tab pair in this container format (prefix "snds"
 * or "music"). `rate` is stored as given. */
int  mc_sndbank_load_named(const char *game_dir, const char *prefix, int set, int n, int rate, mc_sndbank *out);
/* Parse an already loaded .tab / .dat pair (the .tab may still be RNC-compressed). Takes ownership
 * of nothing: `dat` is copied into the bank. Returns the record count (0 = malformed). */
size_t mc_sndbank_parse(const uint8_t *tab, size_t tab_len, const uint8_t *dat, size_t dat_len, mc_sndbank *out);
void mc_sndbank_free(mc_sndbank *b);

/* Number of samples = records after the header record (sound_bank_relocate_5ca58's DAT_0012e246). */
static inline int mc_sndbank_count(const mc_sndbank *b) { return b->record_count ? (int)b->record_count - 1 : 0; }
/* Playable bytes of sample `index` (1..count): pointer and length - 0x10, clipped to the .dat.
 * Returns NULL (and *len = 0) for an index out of range. */
const uint8_t *mc_sndbank_sample(const mc_sndbank *b, int index, uint32_t *len);
const char    *mc_sndbank_name(const mc_sndbank *b, int index);
/* Whole record `index` (1..count) without the -0x10 of the sample players: pointer and the full +0x1a
 * length, clipped to the .dat. The music banks need this - music_play_track_5c0a0 hands the HMI only
 * the pointer and some songs run into the last 16 bytes (CSETUP.ROL / .GEN). NULL when out of range. */
const uint8_t *mc_sndbank_record_data(const mc_sndbank *b, int index, uint32_t *len);

/* Write sample `index` as a mono 8-bit PCM .wav at `rate` Hz (the sanity check of the format).
 * Returns 1 on success. */
int mc_sndbank_write_wav(const mc_sndbank *b, int index, int rate, const char *path);

#ifdef __cplusplus
}
#endif
#endif
