// Action help (docs/13 §3.1, §6.2): one line per Action in the shown language (docs/21),
// the label the Tab menu shows next to a key. Each fits 120 px (MenuTxt, checked at build).
// test_action_help checks that every Action and every kKeymap binding has one, so a
// new Action needs a line in action_help.cpp.
#pragma once

#include "keymap.h"

namespace cpa {

const char* action_help(Action a);  // "" for Action::None (and an Action with no line)

}  // namespace cpa
