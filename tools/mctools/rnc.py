"""RNC ProPack (Rob Northen Compression) method-1 decoder, plus packing via
the `propack` package.

Magic Carpet 1 compresses almost every data file with RNC method 1 ("RNC\\x01").
This decoder is a faithful port of the public-domain dernc.c (Jon Skeet /
Tomasz Lis) algorithm and validates both the packed and unpacked CRC-16s.

Header layout (18 bytes, big-endian):
    0x00  3   "RNC"
    0x03  1   method (1 or 2)
    0x04  4   unpacked length
    0x08  4   packed length (excludes the header)
    0x0C  2   CRC-16 of unpacked data
    0x0E  2   CRC-16 of packed data
    0x10  1   leeway (in-place unpack overlap)
    0x11  1   number of pack chunks
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

RNC_MAGIC = b"RNC"
HEADER_SIZE = 18


class RNCError(ValueError):
    pass


@dataclass
class RNCHeader:
    method: int
    unpacked_len: int
    packed_len: int
    unpacked_crc: int
    packed_crc: int
    leeway: int
    chunks: int

    @classmethod
    def parse(cls, data: bytes) -> "RNCHeader":
        if len(data) < HEADER_SIZE or data[:3] != RNC_MAGIC:
            raise RNCError("not an RNC file")
        method = data[3]
        ul, pl, ucrc, pcrc, leeway, chunks = struct.unpack_from(">IIHHBB", data, 4)
        return cls(method, ul, pl, ucrc, pcrc, leeway, chunks)


def is_rnc(data: bytes) -> bool:
    return len(data) >= HEADER_SIZE and data[:3] == RNC_MAGIC and data[3] in (1, 2)


# --- CRC-16 (reflected, polynomial 0xA001) -------------------------------------
_CRC_TABLE = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ 0xA001 if _c & 1 else _c >> 1
    _CRC_TABLE.append(_c)


def crc16(data: bytes, start: int = 0, length: int | None = None) -> int:
    if length is None:
        length = len(data) - start
    crc = 0
    for b in data[start:start + length]:
        crc = (crc >> 8) ^ _CRC_TABLE[(crc ^ b) & 0xFF]
    return crc


# --- Method 1 bit stream (port of dernc.c) ----------------------------------------
class _BitStream:
    """`pos` always points at the start of the most recently *counted* 16-bit
    word.  The buffer holds `bitcount` valid bits (LSB first) with an extra
    uncounted 16-bit lookahead above them, exactly like the reference."""

    __slots__ = ("data", "pos", "bitbuf", "bitcount")

    def __init__(self, data: bytes, pos: int):
        self.data = data
        self.pos = pos
        self.bitbuf = self._lword(pos)
        self.bitcount = 16

    def _lword(self, p: int) -> int:
        d = self.data
        n = len(d)
        v = 0
        for i in range(4):
            if p + i < n:
                v |= d[p + i] << (8 * i)
        return v

    def peek(self, mask: int) -> int:
        return self.bitbuf & mask

    def advance(self, n: int):
        self.bitbuf >>= n
        self.bitcount -= n
        if self.bitcount < 16:
            self.pos += 2
            self.bitbuf |= self._lword(self.pos) << self.bitcount
            self.bitcount += 16

    def read(self, n: int) -> int:
        v = self.bitbuf & ((1 << n) - 1)
        self.advance(n)
        return v

    def fix(self):
        """Re-sync after raw bytes were consumed from `pos` (dernc bitread_fix)."""
        self.bitcount -= 16
        self.bitbuf &= (1 << self.bitcount) - 1
        self.bitbuf |= self._lword(self.pos) << self.bitcount
        self.bitcount += 16


def _mirror(code: int, nbits: int) -> int:
    r = 0
    for _ in range(nbits):
        r = (r << 1) | (code & 1)
        code >>= 1
    return r


def _read_huftable(bs: _BitStream):
    """Returns list of (codelen, mirrored_code, leaf_value), canonical order."""
    num = bs.read(5)
    if num == 0:
        return []
    leaflen = [bs.read(4) for _ in range(num)]
    table = []
    codeb = 0
    for length in range(1, 17):
        for j in range(num):
            if leaflen[j] == length:
                table.append((length, _mirror(codeb, length), j))
                codeb += 1
        codeb <<= 1
    return table


def _huf_read(bs: _BitStream, table) -> int:
    mask = bs.peek(0xFFFF)
    for codelen, code, val in table:
        if (mask & ((1 << codelen) - 1)) == code:
            bs.advance(codelen)
            if val >= 2:
                base = 1 << (val - 1)
                return base | bs.read(val - 1)
            return val
    raise RNCError("invalid huffman code in stream")


def _unpack_m1(data: bytes, hdr: RNCHeader) -> bytes:
    out = bytearray()
    outlen = hdr.unpacked_len
    bs = _BitStream(data, HEADER_SIZE)
    bs.advance(2)  # lock + encrypt flags (must be zero for our files)
    while len(out) < outlen:
        raw_t = _read_huftable(bs)
        dist_t = _read_huftable(bs)
        len_t = _read_huftable(bs)
        ch_count = bs.read(16)
        while True:
            length = _huf_read(bs, raw_t)
            if length:
                out += data[bs.pos:bs.pos + length]
                bs.pos += length
                bs.fix()
            ch_count -= 1
            if ch_count <= 0:
                break
            posn = _huf_read(bs, dist_t) + 1
            length = _huf_read(bs, len_t) + 2
            if posn > len(out):
                raise RNCError("back-reference before start of output")
            if posn >= length:
                out += out[-posn:len(out) - posn + length]
            else:
                for _ in range(length):
                    out.append(out[-posn])
    return bytes(out[:outlen])


def unpack(data: bytes) -> bytes:
    """Decompress an RNC blob (method 1 natively, method 2 via propack)."""
    hdr = RNCHeader.parse(data)
    if hdr.method != 1:
        return _unpack_with_propack(data)
    pcrc = crc16(data, HEADER_SIZE, hdr.packed_len)
    if pcrc != hdr.packed_crc:
        raise RNCError(f"packed CRC mismatch {pcrc:04x} != {hdr.packed_crc:04x}")
    out = _unpack_m1(data, hdr)
    ucrc = crc16(out)
    if ucrc != hdr.unpacked_crc:
        raise RNCError(f"unpacked CRC mismatch {ucrc:04x} != {hdr.unpacked_crc:04x}")
    return out


def _unpack_with_propack(data: bytes) -> bytes:
    import propack  # type: ignore
    return propack.unpack(bytes(data))


def pack(data: bytes, method: int = 1) -> bytes:
    """Compress with the reference ProPack algorithm (via the propack package)."""
    import propack  # type: ignore
    return propack.pack(bytes(data), method=method)


def unpack_if_rnc(data: bytes) -> bytes:
    return unpack(data) if is_rnc(data) else bytes(data)


def unpack_embedded(data: bytes) -> bytes:
    """Containers with a leading tag before the RNC header: tmaps.dat starts
    with the 8-byte tag "BULLFROG" followed by RNC data."""
    if data[:8] == b"BULLFROG" and is_rnc(data[8:]):
        return unpack(data[8:])
    return unpack_if_rnc(data)


if __name__ == "__main__":
    import sys
    from pathlib import Path

    for name in sys.argv[1:]:
        p = Path(name)
        raw = p.read_bytes()
        out = unpack_embedded(raw)
        print(f"{p.name}: packed={len(raw)} unpacked={len(out)}")
