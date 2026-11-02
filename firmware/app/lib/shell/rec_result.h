// How a recording ended, in words (docs/20 U6, U15): one toast for every end, an automatic
// stop worded apart from a refusal to start ("Meeting: low battery · saved 42:10", never
// "can't record"), and the line of the memo screen held until G0 is released.
#pragma once

#include "app_state.h"
#include "fixed_str.h"
#include "module.h"
#include "rec_core.h"

namespace cpa {

using RecText = FixedStr<80>;

RecText rec_end_text(const rec::RecEffect& ended);  // an Ended effect
ToastLevel rec_end_level(const rec::RecEffect& ended);
const char* rec_held_text(const RecInfo& r);  // the memo ended under a held G0
const char* rec_held_tip(const RecInfo& r);   // "" or what to do instead (the memo cap)
const char* rec_bat_warn_text();              // at or below 3.55 V while recording
RecLast rec_last_of(const rec::RecEffect& e, bool locked = false);  // an Ended or Refuse effect

// docs/20 U6, B1: the lock card's result, counts and reasons only (no name, docs/13 §7.1):
// line "√ Saved 0:12", "× Too short", "× not recorded" (it never started) or
// "× didn't close"; detail why it stopped by itself or did not start ("Meeting: low
// battery", "5 min limit reached", "Needs an SD card (insert, restart)"), "" when the user
// ended it. √ and ×: efontCN_12 has no ✓ or ✗.
struct CardResult {
  RecText line;
  RecText detail;
  bool saved = false;
  bool started = false;  // it recorded (a refusal did not)
};
CardResult rec_card_result(const RecLast& l);

}  // namespace cpa
