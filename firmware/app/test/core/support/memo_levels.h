// Four voice memos recorded on the device (v0.4.0 + the Mando theme, 2026-10-22: handheld,
// normal voice, the v0.3.1 microphone gain), block by block (2048 samples), levels only, no
// audio. `rms` is the block's RMS as recorded (the bytes of the WAV, unfiltered): it tells the
// words from the room. Speech is RMS 155-1083 (median 454, about -37 dBFS), the room between
// words 9-34, and every memo's first block (575-600) is the codec settling as the microphone
// starts, not a voice (docs/19 §6.6). `level` is what the level history sees: the block through
// the level path's high-pass (rec::level_of, 130 Hz, its state carried from block to block, as
// the WAVs played through it on the host), words 136-1056 (median 390, about -38.5 dBFS), the
// room 5-33, the first blocks 265-277.
#pragma once

#include <cstdint>

namespace memo_levels {

struct Memo {
  const uint16_t* rms;    // as recorded: which blocks are words, which the room
  const uint16_t* level;  // through the high-pass: what the waveform and Mando see
  uint16_t n;
};

constexpr uint16_t kMemo0[] = {600, 16,  68,  784, 427, 881, 728, 378, 845, 905, 409, 467, 634, 493, 897,
                               304, 385, 503, 232, 25,  16,  24,  22,  14,  409, 790, 828, 906, 846, 617,
                               763, 545, 833, 450, 342, 780, 592, 707, 870, 623, 410, 55,  13,  11};
constexpr uint16_t kMemo1[] = {592, 16, 19, 129, 656, 632, 569, 563};
constexpr uint16_t kMemo2[] = {575, 10,  16,  88,  659, 459, 632, 155, 547, 542, 317, 257, 539, 225, 451, 229,
                               412, 488, 629, 515, 426, 538, 204, 392, 638, 578, 112, 18,  10,  10,  10,  9,
                               10,  129, 884, 561, 588, 168, 749, 607, 350, 82,  141, 919, 408, 1083, 729, 399,
                               240, 482, 260, 787, 828, 354, 48,  491, 431, 270, 376, 522, 315, 346, 499, 527,
                               379, 496, 239, 467, 273, 371, 760, 381, 24,  17,  14,  11,  346, 275, 356, 572,
                               434, 236, 324, 89,  321, 172, 113, 187, 58,  18,  13,  27};
constexpr uint16_t kMemo3[] = {595, 195, 665, 1002, 574, 757, 580, 819, 597, 527, 612, 628, 477, 358, 267, 646, 348,
                               693, 232, 362, 480,  251, 655, 476, 315, 299, 275, 473, 168, 576, 387, 331, 145, 339,
                               524, 329, 254, 377,  525, 332, 477, 387, 256, 590, 587, 303, 397, 192, 228, 280, 256,
                               28,  34,  29,  18,   734, 399, 761, 720, 266, 460, 537, 386, 471, 369, 438, 498, 251,
                               259, 287, 491, 524,  377, 310, 292, 275, 299, 208, 350, 354, 392, 230, 98,  49};

constexpr uint16_t kLevel0[] = {277, 12,  65,  728, 396, 832, 688, 287, 793, 850, 394, 438, 574, 407, 842, 270, 362,
                                492, 216, 22,  14,  21,  19,  12,  367, 750, 744, 776, 690, 523, 649, 424, 745, 327,
                                255, 742, 551, 644, 794, 576, 369, 52,  10,  8};
constexpr uint16_t kLevel1[] = {276, 10,  14,  128, 585, 565, 546, 496};
constexpr uint16_t kLevel2[] = {265,  5,    13,   83,   591,  443,  598,  155,  503,  500,  279,  201,  485,  171,
                                434,  218,  381,  436,  589,  468,  369,  474,  156,  370,  619,  546,  94,   14,
                                8,    7,    7,    6,    6,    104,  798,  441,  521,  136,  701,  569,  331,  76,
                                141,  879,  325,  1056, 711,  342,  202,  390,  213,  744,  790,  341,  47,   369,
                                390,  247,  316,  481,  290,  295,  441,  461,  304,  447,  225,  429,  244,  311,
                                723,  364,  22,   14,   13,   7,    310,  223,  274,  542,  416,  224,  288,  83,
                                295,  160,  80,   171,  53,   15,   7,    21};
constexpr uint16_t kLevel3[] = {275, 147, 604, 955, 519, 711, 534, 769, 529, 504, 515, 554, 379, 313, 233, 552, 317,
                                630, 194, 332, 380, 209, 584, 389, 288, 284, 252, 445, 164, 501, 340, 276, 142, 233,
                                485, 290, 209, 315, 459, 295, 420, 339, 203, 542, 555, 251, 344, 166, 205, 243, 226,
                                27,  33,  28,  10,  673, 353, 732, 667, 223, 416, 482, 317, 415, 327, 348, 366, 221,
                                218, 236, 454, 503, 315, 253, 266, 242, 258, 173, 248, 244, 279, 212, 61,  19};

static_assert(sizeof kLevel0 == sizeof kMemo0 && sizeof kLevel1 == sizeof kMemo1, "a level a block");
static_assert(sizeof kLevel2 == sizeof kMemo2 && sizeof kLevel3 == sizeof kMemo3, "a level a block");

constexpr Memo kMemos[] = {{kMemo0, kLevel0, sizeof kMemo0 / sizeof kMemo0[0]},
                           {kMemo1, kLevel1, sizeof kMemo1 / sizeof kMemo1[0]},
                           {kMemo2, kLevel2, sizeof kMemo2 / sizeof kMemo2[0]},
                           {kMemo3, kLevel3, sizeof kMemo3 / sizeof kMemo3[0]}};

constexpr uint16_t kSpeech = 150;  // a block this loud (rms) is a word (the quietest is 155)
constexpr uint16_t kRoom = 40;     // a block quieter (rms) is the room (the loudest is 34)

}  // namespace memo_levels
