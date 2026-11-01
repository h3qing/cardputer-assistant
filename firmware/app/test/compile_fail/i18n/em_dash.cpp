// expect: txt_missing_glyph
// efontCN_12 has no em dash: it would draw □.
#include "txt.h"

constexpr cpa::Txt kX{"一", "one — two"};
