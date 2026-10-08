// Screenshot files for mcport (Phase 4, round 10 task D; port-only). No SDL, no zlib.
//
// PNG: 8-bit RGB, one IDAT with *stored* (uncompressed) deflate blocks - every viewer reads it, the file is
// the raw image plus 6 bytes per 64 KiB and a filter byte per row. PPM (P6) as MC_SHOT always wrote.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

bool shot_write_png(const char *path, int w, int h, const uint8_t *rgb);   // rgb: w*h*3 bytes, rows top down
bool shot_write_ppm(const char *path, int w, int h, const uint8_t *rgb);
// The PNG bytes shot_write_png writes.
std::vector<uint8_t> shot_encode_png(int w, int h, const uint8_t *rgb);
// Reads back a PNG that shot_encode_png made (stored deflate only; tests). False on any other PNG / a bad CRC.
bool shot_decode_png(const std::vector<uint8_t> &png, int *w, int *h, std::vector<uint8_t> *rgb);
uint32_t shot_crc32(const uint8_t *p, size_t n, uint32_t crc = 0);

// The file name of a `shot [name]` request: "" -> "shot_<YYYYmmdd_HHMMSS>_<n>.png"; a name without an
// extension gets ".png"; ".ppm" selects PPM. A relative name goes into `dir`, an absolute one is kept.
std::string shot_resolve_path(const std::string &dir, const std::string &name, int counter);
