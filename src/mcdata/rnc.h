/* RNC ProPack method-1 decompressor (port of dernc, validated against the
 * Python implementation in tools/mctools/rnc.py). */
#ifndef MC_RNC_H
#define MC_RNC_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNC_HEADER_SIZE 18

typedef struct {
    uint8_t  method;
    uint32_t unpacked_len;
    uint32_t packed_len;
    uint16_t unpacked_crc;
    uint16_t packed_crc;
    uint8_t  leeway;
    uint8_t  chunks;
} rnc_header;

enum {
    RNC_OK = 0,
    RNC_ERR_NOT_RNC = -1,
    RNC_ERR_METHOD = -2,
    RNC_ERR_PACKED_CRC = -3,
    RNC_ERR_UNPACKED_CRC = -4,
    RNC_ERR_CORRUPT = -5,
    RNC_ERR_OUTBUF = -6,
};

int      rnc_is_rnc(const uint8_t *data, size_t len);
int      rnc_parse_header(const uint8_t *data, size_t len, rnc_header *out);
uint16_t rnc_crc16(const uint8_t *data, size_t len);

/* Decompress `data` (starting at the RNC header) into `out` (capacity out_cap).
 * Returns bytes written (== header.unpacked_len) or a negative RNC_ERR_*. */
long rnc_unpack(const uint8_t *data, size_t len, uint8_t *out, size_t out_cap);

/* Convenience: allocates the output with malloc(); caller frees. Returns NULL on error
 * and stores the error code in *err if non-NULL. */
uint8_t *rnc_unpack_alloc(const uint8_t *data, size_t len, size_t *out_len, int *err);

#ifdef __cplusplus
}
#endif
#endif
