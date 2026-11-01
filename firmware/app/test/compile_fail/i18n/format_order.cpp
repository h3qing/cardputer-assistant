// expect: txt_format_mismatch
// The arguments cannot be swapped by a translation.
#include "txt.h"

constexpr cpa::Txt kX{"%u %s", "%s %u"};
