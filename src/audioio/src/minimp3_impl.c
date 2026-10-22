/* The single translation unit that carries the minimp3 implementation (CC0, third_party/minimp3).
 * Compiled with warnings disabled: foreign code, pinned by submodule, not ours to restyle.
 * MINIMP3_FLOAT_OUTPUT / MINIMP3_NO_STDIO come from the radiff::minimp3 interface target so
 * every includer agrees on mp3d_sample_t. */
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
