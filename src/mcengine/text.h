// Language strings: the table DAT_000adce8 (80 char pointers filled by text_split_lines_3ed30 over
// data/{e,f,g,i}text.dat according to Config.language). The function keys (input.cpp), the player
// notices (player.cpp) and the HUD copy their on-screen text from it. Owner: text.cpp.
#pragma once
#include <cstddef>

constexpr int MC_TEXT_COUNT = 80;

// The language switch of fe_screen_language_56940 + text_split_lines_3ed30(text, DAT_000adce8, 0x50).
// language 0 English, 1 French, 2 German, 3 Italian. False when the file is missing.
bool        text_load(const char *game_dir, int language);
// Entry `index` of the table ("" until text_load succeeded or when out of range).
const char *text_get(int index);
// strcpy of text_get(index) into a fixed buffer (the original's strcpy is unbounded).
void        text_copy(char *dst, size_t cap, int index);

// Indices the game code uses (DAT_000adce8 + 4 * index):
enum {
    MC_TEXT_HAS_DIED        = 54,   // 0xaddc0 "has died."
    MC_TEXT_ELIMINATED      = 62,   // 0xadde0 "has been eliminated from the realm."
};
