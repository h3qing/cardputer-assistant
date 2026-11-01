// expect: txt_format_mismatch
// %u against %s.
#include "txt.h"

constexpr cpa::Txt kX{"%u 条", "%s notes"};
