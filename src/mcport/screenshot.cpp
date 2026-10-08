// Screenshot files (screenshot.h; port round 10 task D).
#include "screenshot.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace {

void put32be(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x);
}
uint32_t get32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

void chunk(std::vector<uint8_t> &out, const char *type, const uint8_t *data, size_t n) {
    put32be(out, (uint32_t)n);
    const size_t at = out.size();
    out.insert(out.end(), type, type + 4);
    if (n) out.insert(out.end(), data, data + n);
    put32be(out, shot_crc32(out.data() + at, n + 4));
}

bool write_file(const char *path, const void *data, size_t n) {
    FILE *f = nullptr;
#ifdef _WIN32
    if (fopen_s(&f, path, "wb") != 0) f = nullptr;
#else
    f = std::fopen(path, "wb");
#endif
    if (!f) return false;
    const bool ok = std::fwrite(data, 1, n, f) == n;
    return std::fclose(f) == 0 && ok;
}

const uint8_t k_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

}  // namespace

uint32_t shot_crc32(const uint8_t *p, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

std::vector<uint8_t> shot_encode_png(int w, int h, const uint8_t *rgb) {
    std::vector<uint8_t> out(k_sig, k_sig + 8);
    uint8_t ihdr[13] = {};
    for (int i = 0; i < 4; i++) { ihdr[i] = (uint8_t)((uint32_t)w >> (24 - 8 * i)); ihdr[4 + i] = (uint8_t)((uint32_t)h >> (24 - 8 * i)); }
    ihdr[8] = 8;        // bit depth
    ihdr[9] = 2;        // colour type RGB
    chunk(out, "IHDR", ihdr, sizeof ihdr);
    // raw scanlines: filter byte 0 + the row
    const size_t row = (size_t)w * 3;
    std::vector<uint8_t> raw;
    raw.reserve((row + 1) * (size_t)h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb + row * (size_t)y, rgb + row * (size_t)(y + 1));
    }
    // zlib stream: header, stored blocks of at most 65535 bytes, adler32
    std::vector<uint8_t> z;
    z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    do {
        const size_t n = raw.size() - pos < 65535 ? raw.size() - pos : 65535;
        const bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)n); z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)~n); z.push_back((uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + (std::ptrdiff_t)pos, raw.begin() + (std::ptrdiff_t)(pos + n));
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32be(z, b << 16 | a);
    chunk(out, "IDAT", z.data(), z.size());
    chunk(out, "IEND", nullptr, 0);
    return out;
}

bool shot_decode_png(const std::vector<uint8_t> &png, int *w, int *h, std::vector<uint8_t> *rgb) {
    if (png.size() < 8 || std::memcmp(png.data(), k_sig, 8) != 0) return false;
    size_t p = 8;
    std::vector<uint8_t> z;
    int W = 0, H = 0;
    bool end = false;
    while (p + 12 <= png.size() && !end) {
        const uint32_t n = get32be(&png[p]);
        if (p + 12 + n > png.size()) return false;
        const uint8_t *type = &png[p + 4];
        if (shot_crc32(type, n + 4) != get32be(&png[p + 8 + n])) return false;
        const uint8_t *d = type + 4;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (n != 13 || d[8] != 8 || d[9] != 2) return false;
            W = (int)get32be(d); H = (int)get32be(d + 4);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            z.insert(z.end(), d, d + n);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            end = true;
        }
        p += 12 + n;
    }
    if (!end || W <= 0 || H <= 0 || z.size() < 6) return false;
    std::vector<uint8_t> raw;
    size_t q = 2;
    for (;;) {
        if (q + 5 > z.size()) return false;
        const uint8_t hdr = z[q];
        if ((hdr & 6) != 0) return false;              // stored blocks only
        const size_t n = z[q + 1] | (size_t)z[q + 2] << 8;
        if ((uint16_t)~n != (uint16_t)(z[q + 3] | z[q + 4] << 8) || q + 5 + n > z.size()) return false;
        raw.insert(raw.end(), z.begin() + (std::ptrdiff_t)(q + 5), z.begin() + (std::ptrdiff_t)(q + 5 + n));
        q += 5 + n;
        if (hdr & 1) break;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    if (q + 4 > z.size() || get32be(&z[q]) != (b << 16 | a)) return false;
    const size_t row = (size_t)W * 3;
    if (raw.size() != (row + 1) * (size_t)H) return false;
    rgb->clear();
    for (int y = 0; y < H; y++) {
        if (raw[(row + 1) * (size_t)y] != 0) return false;
        rgb->insert(rgb->end(), raw.begin() + (std::ptrdiff_t)((row + 1) * (size_t)y + 1), raw.begin() + (std::ptrdiff_t)((row + 1) * (size_t)(y + 1)));
    }
    *w = W; *h = H;
    return true;
}

bool shot_write_png(const char *path, int w, int h, const uint8_t *rgb) {
    const std::vector<uint8_t> png = shot_encode_png(w, h, rgb);
    return write_file(path, png.data(), png.size());
}

bool shot_write_ppm(const char *path, int w, int h, const uint8_t *rgb) {
    char hdr[32];
    const int hn = std::snprintf(hdr, sizeof hdr, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> v(hdr, hdr + hn);
    v.insert(v.end(), rgb, rgb + (size_t)w * h * 3);
    return write_file(path, v.data(), v.size());
}

std::string shot_resolve_path(const std::string &dir, const std::string &name, int counter) {
    namespace fs = std::filesystem;
    std::string n = name;
    if (n.empty()) {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char b[64];
        std::strftime(b, sizeof b, "shot_%Y%m%d_%H%M%S", &tm);
        n = std::string(b) + "_" + std::to_string(counter);
    }
    fs::path p(n);
    if (!p.has_extension()) p += ".png";
    if (p.is_relative() && !dir.empty()) p = fs::path(dir) / p;
    return p.string();
}
