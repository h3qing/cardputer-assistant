// GhostFilter (05 §5.7 rule 3, 04 US-13 AC1): the keyboard has no diodes, so three
// held keys at three corners of a matrix rectangle make the fourth corner read as
// pressed. A frame holding a full rectangle is ambiguous: it is dropped whole (the
// last good set is kept) and counted once in ghost_n.
#pragma once

#include <cstdint>

#include "key_set.h"

namespace cpa {

bool has_rectangle(KeySet s);  // all four corners of some matrix rectangle are held
bool ghost_risk(KeySet s);     // three corners held: the fourth would appear as a ghost

struct GhostState {
  KeySet last_good;
  KeySet last_raw;
  uint16_t ghost_n = 0;
};

struct GhostStep {
  GhostState state;
  KeySet keys;  // the set to use for this frame
};

GhostStep ghost_filter(const GhostState& s, KeySet raw);

}  // namespace cpa
