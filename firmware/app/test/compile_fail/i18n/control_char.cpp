// expect: txt_control_char
// A newline in a one-line string.
#include "txt.h"

constexpr cpa::Txt kX{"一\n二", "one two"};
