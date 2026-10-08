#include "rnc.h"
#include <stdlib.h>
#include <string.h>

static uint16_t crc_table[256];
static int crc_ready = 0;

static void crc_init(void) {
    for (int i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
        crc_table[i] = c;
    }
    crc_ready = 1;
}

uint16_t rnc_crc16(const uint8_t *data, size_t len) {
    if (!crc_ready) crc_init();
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) crc = (uint16_t)((crc >> 8) ^ crc_table[(crc ^ data[i]) & 0xFF]);
    return crc;
}

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

int rnc_is_rnc(const uint8_t *d, size_t len) {
    return len >= RNC_HEADER_SIZE && d[0] == 'R' && d[1] == 'N' && d[2] == 'C' && (d[3] == 1 || d[3] == 2);
}

int rnc_parse_header(const uint8_t *d, size_t len, rnc_header *h) {
    if (!rnc_is_rnc(d, len)) return RNC_ERR_NOT_RNC;
    h->method = d[3];
    h->unpacked_len = be32(d + 4);
    h->packed_len = be32(d + 8);
    h->unpacked_crc = be16(d + 12);
    h->packed_crc = be16(d + 14);
    h->leeway = d[16];
    h->chunks = d[17];
    return RNC_OK;
}

/* ---- bit stream (see tools/mctools/rnc.py for the model) ---- */
typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;       /* start of the most recently counted 16-bit word */
    uint32_t bitbuf;
    int bitcount;
} bitstream;

static uint32_t lword(const bitstream *bs, size_t p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        if (p + i < bs->len) v |= (uint32_t)bs->data[p + i] << (8 * i);
    return v;
}

static void bs_init(bitstream *bs, const uint8_t *data, size_t len, size_t pos) {
    bs->data = data; bs->len = len; bs->pos = pos;
    bs->bitbuf = lword(bs, pos);
    bs->bitcount = 16;
}

static void bs_advance(bitstream *bs, int n) {
    bs->bitbuf >>= n;
    bs->bitcount -= n;
    if (bs->bitcount < 16) {
        bs->pos += 2;
        bs->bitbuf |= lword(bs, bs->pos) << bs->bitcount;
        bs->bitcount += 16;
    }
}

static uint32_t bs_read(bitstream *bs, int n) {
    uint32_t v = bs->bitbuf & ((1u << n) - 1u);
    bs_advance(bs, n);
    return v;
}

static void bs_fix(bitstream *bs) {
    bs->bitcount -= 16;
    bs->bitbuf &= (1u << bs->bitcount) - 1u;
    bs->bitbuf |= lword(bs, bs->pos) << bs->bitcount;
    bs->bitcount += 16;
}

typedef struct { uint8_t codelen; uint16_t code; uint8_t value; } huf_entry;
typedef struct { int num; huf_entry e[32]; } huf_table;

static uint16_t mirror(uint16_t code, int nbits) {
    uint16_t r = 0;
    for (int i = 0; i < nbits; i++) { r = (uint16_t)((r << 1) | (code & 1)); code >>= 1; }
    return r;
}

static void read_huftable(bitstream *bs, huf_table *h) {
    int num = (int)bs_read(bs, 5);
    h->num = 0;
    if (!num) return;
    uint8_t leaflen[32];
    for (int i = 0; i < num; i++) leaflen[i] = (uint8_t)bs_read(bs, 4);
    uint16_t codeb = 0;
    for (int length = 1; length <= 16; length++) {
        for (int j = 0; j < num; j++) {
            if (leaflen[j] == length) {
                huf_entry *e = &h->e[h->num++];
                e->codelen = (uint8_t)length;
                e->code = mirror(codeb, length);
                e->value = (uint8_t)j;
                codeb++;
            }
        }
        codeb <<= 1;
    }
}

static long huf_read(bitstream *bs, const huf_table *h) {
    uint32_t mask = bs->bitbuf & 0xFFFF;
    for (int i = 0; i < h->num; i++) {
        const huf_entry *e = &h->e[i];
        if ((mask & ((1u << e->codelen) - 1u)) == e->code) {
            bs_advance(bs, e->codelen);
            if (e->value >= 2) {
                uint32_t base = 1u << (e->value - 1);
                return (long)(base | bs_read(bs, e->value - 1));
            }
            return e->value;
        }
    }
    return -1;
}

long rnc_unpack(const uint8_t *data, size_t len, uint8_t *out, size_t out_cap) {
    rnc_header h;
    int r = rnc_parse_header(data, len, &h);
    if (r) return r;
    if (h.method != 1) return RNC_ERR_METHOD;
    if ((size_t)h.packed_len + RNC_HEADER_SIZE > len) return RNC_ERR_CORRUPT;
    if (out_cap < h.unpacked_len) return RNC_ERR_OUTBUF;
    if (rnc_crc16(data + RNC_HEADER_SIZE, h.packed_len) != h.packed_crc) return RNC_ERR_PACKED_CRC;

    bitstream bs;
    bs_init(&bs, data, RNC_HEADER_SIZE + h.packed_len, RNC_HEADER_SIZE);
    bs_advance(&bs, 2);
    size_t op = 0;
    huf_table raw, dist, lent;
    while (op < h.unpacked_len) {
        read_huftable(&bs, &raw);
        read_huftable(&bs, &dist);
        read_huftable(&bs, &lent);
        long ch_count = (long)bs_read(&bs, 16);
        for (;;) {
            long length = huf_read(&bs, &raw);
            if (length < 0) return RNC_ERR_CORRUPT;
            if (length) {
                if (op + (size_t)length > out_cap || bs.pos + (size_t)length > bs.len) return RNC_ERR_CORRUPT;
                memcpy(out + op, data + bs.pos, (size_t)length);
                op += (size_t)length;
                bs.pos += (size_t)length;
                bs_fix(&bs);
            }
            if (--ch_count <= 0) break;
            long posn = huf_read(&bs, &dist);
            if (posn < 0) return RNC_ERR_CORRUPT;
            posn += 1;
            length = huf_read(&bs, &lent);
            if (length < 0) return RNC_ERR_CORRUPT;
            length += 2;
            if ((size_t)posn > op || op + (size_t)length > out_cap) return RNC_ERR_CORRUPT;
            for (long i = 0; i < length; i++) { out[op] = out[op - (size_t)posn]; op++; }
        }
    }
    if (rnc_crc16(out, h.unpacked_len) != h.unpacked_crc) return RNC_ERR_UNPACKED_CRC;
    return (long)h.unpacked_len;
}

uint8_t *rnc_unpack_alloc(const uint8_t *data, size_t len, size_t *out_len, int *err) {
    rnc_header h;
    int r = rnc_parse_header(data, len, &h);
    if (r) { if (err) *err = r; return NULL; }
    uint8_t *out = (uint8_t *)malloc(h.unpacked_len ? h.unpacked_len : 1);
    if (!out) { if (err) *err = RNC_ERR_OUTBUF; return NULL; }
    long n = rnc_unpack(data, len, out, h.unpacked_len);
    if (n < 0) { free(out); if (err) *err = (int)n; return NULL; }
    if (out_len) *out_len = (size_t)n;
    if (err) *err = RNC_OK;
    return out;
}
