// expect: txt_too_wide
// A toast over 224 px in English (38 chars = 228 px).
#include "txt.h"

constexpr cpa::ToastTxt kX{"已保存", "Saved to the outbox, waiting for a Mac"};
