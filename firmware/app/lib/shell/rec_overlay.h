// Recording texts (docs/11 §3, §4.2-§4.3), shared by the overlays (rec_overlay.cpp,
// rec_pages.cpp), the status bar and the runtime's toasts.
#pragma once

#include <cstdint>

#include "app_state.h"
#include "canvas.h"
#include "fixed_str.h"
#include "rec_core.h"
#include "theme_anim.h"
#include "theme_levels.h"
#include "theme_mando.h"
#include "txt.h"

namespace cpa {

// The ● rule (docs/13 §7.2): in these phases ● and the timer are always on screen.
bool rec_on_screen(RecPhase p);
const char* rec_refusal_text(rec::RecRefusal why);
FixedStr<16> rec_clock_text(uint32_t elapsed_ms);  // "12:31", minutes past 59 as is
// docs/20 U15, B2: a memo in its last kMemoWarnMs (from 4:30) counts down, in Warn.
constexpr uint32_t kMemoWarnMs = 30000;
bool rec_memo_late(const RecInfo& r, uint32_t now);
// docs/20 U6, B2: about how many minutes the battery records for before the 3.45 V stop, in
// steps of 5 (0: under 5). An estimate, kBatMvPerMin, until G-A7 measures the curve.
int rec_bat_minutes(uint16_t mv);

// v0.5 (docs/19 §6.6-§6.7, §7.2): the memo and meeting pages, with Mando.
constexpr uint16_t kVoiceLevel = 104;  // a block this loud (RMS, -50 dBFS: theme::kVoiceOn) is someone talking
FixedStr<16> rec_tag_text(const AppState& s);  // where it goes: "No project", "#cpa", "#3"
// Cue: he sits; Recording: nods while there is a voice, else tilts his head; EndConfirm:
// sits; Finishing: sniffs (saving). rec_mando judges the voice by r.level, a tick by the
// level history's (theme::level_voice).
theme::MandoAct rec_mando_act(RecPhase p, bool voice);
theme::MandoAct rec_mando(const RecInfo& r);
const char* rec_sign(const RecInfo& r);  // "Listen", "Saving…"; nullptr in Cue (no sign)

// v0.5 S3 (docs/19 §3.5): what the pages draw between ticks: the level history the last tick
// took (the memo's waveform, the meeting's timeline) and Mando as the last tick showed him.
struct RecLook {
  const theme::LevelHistory* levels = nullptr;
  theme::MandoView mando;
};
void render_memo_page(Canvas& c, const AppState& s, const RecLook& look, uint32_t now);
void render_meeting_page(Canvas& c, const AppState& s, const RecLook& look, uint32_t now);
// docs/11 §3.2: the text exactly as v0.4.0; docs/20 B2: under 30 % a line of how long the
// battery lasts between its rows.
void render_meeting_confirm(Canvas& c, const AppState& s);
// The confirms' key lines, v0.4.0's text, as keycaps on the cards (docs/20 B2: the hint bar
// no longer repeats them; it names Opt+L).
constexpr LineTxt kMeetingConfirmKeys{"Enter 开始    ` 取消", "[Enter]start    [`]cancel"};
constexpr LineTxt kEndConfirmKeys{"Enter 结束    ` 继续录", "[Enter]stop    [`]keep going"};

// gh #53: the room the pages' legend of 1-9 and 0 leaves (rec_legend.h): under three rows
// the memo's waveform is 2/3 as tall; under two the meeting's timeline has no 0:00 and "now".
// The ticks ask it, from the state, so they draw only where the last full frame put them.
struct RecRoom {
  bool low_wave = false;
  bool no_labels = false;
};
RecRoom rec_room(const Canvas& c, const AppState& s);

// What a tick redraws on the pages, over what is there; each returns the rectangle to push.
Rect rec_mando_part(Canvas& c, const RecInfo& r, const theme::MandoView& v);  // Mando's sprite
// The memo's waveform, in the room the legend left it.
Rect rec_wave_part(Canvas& c, const theme::LevelHistory& h, bool late = false, const RecRoom& room = RecRoom{});
// The meeting's timeline from was to now: only what changed, in up to two rectangles (the
// bars and the dot, and "now" when the dot moved; n == 0: nothing).
struct RecParts {
  uint8_t n = 0;
  Rect r[2];
};
RecParts rec_line_part(Canvas& c, const theme::LevelHistory& was, const theme::LevelHistory& now,
                       const RecRoom& room = RecRoom{});
Rect rec_dot_rect(const RecInfo& r);  // the big ●, pushed after a breath step

}  // namespace cpa
