// Language strings (see text.h). Moved out of input.cpp in port round 4.
#include "text.h"
#include "mcfile.h"
#include <cstring>
#include <vector>

static std::vector<char> s_text_data;
static const char *s_text[MC_TEXT_COUNT];

bool text_load(const char *game_dir, int language) {
    static const char *const files[4] = {"data/etext.dat", "data/ftext.dat", "data/gtext.dat", "data/itext.dat"};
    if (language < 0 || language > 3) return false;
    char path[1024];
    mc_path_join(path, sizeof path, game_dir, files[language]);
    mc_blob blob;
    if (!mc_read_unpacked(path, &blob)) return false;
    s_text_data.assign(blob.len + 1, 0);                // one terminator behind the file
    if (blob.len != 0) std::memcpy(s_text_data.data(), blob.data, blob.len);
    mc_blob_free(&blob);
    const char *p = s_text_data.data();
    const char *end = p + s_text_data.size() - 1;       // the terminator added above
    for (int i = 0; i < MC_TEXT_COUNT; i++) {
        s_text[i] = p;
        while (p < end && *p != 0) p++;
        if (p < end) p++;
    }
    return true;
}

const char *text_get(int index) {
    if (index < 0 || index >= MC_TEXT_COUNT || s_text[index] == nullptr) return "";
    return s_text[index];
}

void text_copy(char *dst, size_t cap, int index) {
    if (cap == 0) return;
    const char *src = text_get(index);
    size_t i = 0;
    for (; i < cap - 1 && src[i] != 0; i++) dst[i] = src[i];
    dst[i] = 0;
}
