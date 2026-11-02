// The recording overlays (docs/11 §3.1-§3.2, §4.2): the memo HUD, the meeting consent
// confirm, the meeting page and the end confirm. ● and the clock are drawn on every
// recording screen, also behind the end confirm, also when the backlight is dimmed: the
// status bar's ●mm:ss always, and the pages' big ● and timer (rec_pages.cpp, docs/19 §6.6).
#include "rec_overlay.h"

#include <cstdio>

#include "protocol_limits.h"
#include "rec_result.h"
#include "reducers.h"
#include "shell.h"
#include "status_bar.h"
#include "text_fit.h"
#include "shell_hints.h"
#include "theme.h"
#include "txt.h"

namespace cpa {

// docs/21: the recording overlays' words. The hints bracket their keys in English.
constexpr Txt kNoProject{"未归属", "No project"};
constexpr SignTxt kSaving{"保存中…", "Saving…"};
constexpr SignTxt kListening{"在听", "Listen"};
constexpr LineTxt kBatUnder5{"电量不够 5 分钟", "Battery: under 5 min"};
constexpr Txt kBatMinutes{"电量约够 %d 分钟", "Battery: about %d min"};
constexpr LineTxt kStartMeeting{"开始会议录音？", "Start a meeting recording?"};
constexpr LineTxt kTellEveryone{"请先告知在场的人", "Tell everyone here first"};
constexpr ToastTxt kRefNoSd{"录音需要 SD 卡：插卡后重启", "Needs an SD card: insert, restart"};
constexpr ToastTxt kRefCardFull{"SD 卡已满", "SD card full"};
constexpr ToastTxt kRefPending{"待同步录音太多，请先连 Mac 同步", "Too many to sync: connect the Mac"};
constexpr ToastTxt kRefBattery{"电量低，不能录音", "Battery low: can't record"};
constexpr ToastTxt kRefNoMemory{"内存不足，不能录音", "Out of memory: can't record"};
constexpr ToastTxt kRefNoIds{"NVS 错误：不能录音", "NVS error: can't record"};
constexpr ToastTxt kRefCardError{"SD 卡出错，录音已停止", "SD card error: recording stopped"};
constexpr ToastTxt kRefIncomplete{"卡上段太多：先同步，再重启", "Too many files: sync, then restart"};
constexpr ToastTxt kRefTimeout{"录音收尾超时：已写下的段还在卡上", "Didn't close: parts are on the card"};

FixedStr<16> rec_tag_text(const AppState& s) {
  const Opt<ProjectRow> p = project_by_slot(s.projects, s.rec.slot);
  if (p.has) return FixedStr<16>::from("#").append(p.value.key.c_str());
  if (s.rec.slot == 0) return FixedStr<16>::from(tr(kNoProject));
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%u", static_cast<unsigned>(s.rec.slot));
  return FixedStr<16>::from(buf);
}

theme::MandoAct rec_mando_act(RecPhase p, bool voice) {
  switch (p) {
    case RecPhase::Recording: return voice ? theme::MandoAct::Listen : theme::MandoAct::Tilt;
    case RecPhase::Finishing: return theme::MandoAct::Sniff;
    default: return theme::MandoAct::Idle;
  }
}

theme::MandoAct rec_mando(const RecInfo& r) { return rec_mando_act(r.phase, r.level >= kVoiceLevel); }

const char* rec_sign(const RecInfo& r) {
  if (r.phase == RecPhase::Cue) return nullptr;
  return tr(r.phase == RecPhase::Finishing ? kSaving : kListening);
}

namespace {

constexpr int kBatMvPerMin = 2;  // docs/20 U6: a guess on the safe side until G-A7 measures it

// docs/20 U6, B2: under 30 %, how long the battery lasts (the meeting stops itself at 3.45 V).
void draw_battery_line(Canvas& c, uint16_t mv) {
  const int pct = battery_pct(mv);
  if (pct < 0 || pct >= kBatWarnPct) return;
  const int minutes = rec_bat_minutes(mv);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%s", tr(kBatUnder5));
  if (minutes > 0) std::snprintf(buf, sizeof buf, tr(kBatMinutes), minutes);
  c.text(12, 64, buf, Style{Color::Warn, Color::Card});
}

}  // namespace

// The meeting consent confirm (docs/11 §3.2): v0.4.0's text and rows, on a card, the keys
// as keycaps; Mando sits at its right, clear of the words (docs/19 §6.7).
void render_meeting_confirm(Canvas& c, const AppState& s) {
  theme::card(c, Rect{4, 14, 232, 106});
  c.text(12, 24, tr(kStartMeeting), Style{Color::Warn, Color::Card});
  c.text(12, 48, tr(kTellEveryone), Style{Color::Ink, Color::Card});
  draw_battery_line(c, s.device.bat_mv);
  theme::keys_text(c, 12, 84, tr(kMeetingConfirmKeys), Color::Ink, Color::Card);
  theme::mando(c, 190, 40, theme::MandoSize::M46, theme::MandoAct::Idle, 0, Color::Card);
}

bool rec_on_screen(RecPhase p) {
  return p == RecPhase::Cue || p == RecPhase::Recording || p == RecPhase::EndConfirm || p == RecPhase::Finishing;
}

const char* rec_refusal_text(rec::RecRefusal why) {
  switch (why) {
    case rec::RecRefusal::NoSd: return tr(kRefNoSd);  // docs/20 U16
    case rec::RecRefusal::CardFull: return tr(kRefCardFull);
    case rec::RecRefusal::Pending: return tr(kRefPending);
    case rec::RecRefusal::Battery: return tr(kRefBattery);
    case rec::RecRefusal::NoMemory: return tr(kRefNoMemory);
    case rec::RecRefusal::NoIds: return tr(kRefNoIds);
    case rec::RecRefusal::CardError: return tr(kRefCardError);
    case rec::RecRefusal::Timeout: return tr(kRefTimeout);  // docs/20 S1
    case rec::RecRefusal::Incomplete: return tr(kRefIncomplete);  // docs/28 D15
    default: return "";
  }
}

bool rec_memo_late(const RecInfo& r, uint32_t now) {
  return r.mode == RecMode::Memo && r.phase == RecPhase::Recording &&
         now - r.started_ms >= limits::kMemoMaxMs - kMemoWarnMs;
}

int rec_bat_minutes(uint16_t mv) {
  if (mv <= rec::kBatStopMv) return 0;
  const int minutes = (mv - rec::kBatStopMv) / kBatMvPerMin;
  return minutes / 5 * 5;
}

FixedStr<16> rec_clock_text(uint32_t elapsed_ms) {
  const unsigned s = elapsed_ms / 1000;
  char buf[16];
  std::snprintf(buf, sizeof buf, "%u:%02u", s / 60, s % 60);
  return FixedStr<16>::from(buf);
}

namespace {

// mockup 04, and gh #51: what G0 does now (held, let go within the grace, hands free; the
// last with the digits and Opt+L, its pocket key; Tab opens the menu there too). 0 "none" is
// the legend's (gh #53), right above: not said twice.
const char* memo_hint(const RecInfo& r) {
  if (r.latched) return tr(txt::kMemoLatchedHint);
  if (r.grace) return tr(txt::kMemoGraceHint);
  return tr(txt::kMemoHint);
}

// docs/20 U15: the memo ended under a held G0. Its words stay until the release, on a card
// as the confirm boxes are (docs/19 §6.7); nothing on it moves (shell_anim.cpp).
void render_memo_held(Canvas& c, const RecInfo& r) {
  theme::card(c, Rect{4, 14, 232, 106});
  c.text(12, 24, rec_held_text(r), Style{Color::Warn, Color::Card});
  theme::keys_text(c, 12, 48, tr(txt::kLetGoG0), Color::Ink, Color::Card);  // G0 a keycap, as in the hint bar
  draw_fit(c, 12, 84, 216, rec_held_tip(r), Style{Color::Ink3, Color::Card});
}

}  // namespace

// The pages as the last animation tick left them (shell_anim.cpp).
void Shell::render_rec(Canvas& c, uint32_t now) const {
  const AppState& s = state();
  if (s.rec.held) return render_memo_held(c, s.rec);
  if (s.rec.phase == RecPhase::Confirm) return render_meeting_confirm(c, s);
  const RecLook look{&shown_, theme::mando_view(mando_, rec_mando(s.rec))};
  if (s.rec.mode == RecMode::Memo) return render_memo_page(c, s, look, now);
  render_meeting_page(c, s, look, now);
}

const char* Shell::rec_hint() const {
  const RecInfo& r = state().rec;
  if (r.held) return tr(txt::kLetGoG0);
  // docs/20 B2: the boxes' cards say Enter and `; the bar names what else there is (D3)
  switch (r.phase) {
    case RecPhase::Confirm: return tr(txt::kStartBoxHint);
    case RecPhase::EndConfirm: return tr(txt::kEndBoxHint);
    case RecPhase::Finishing: return "";  // Mando's sign says "Saving…"
    default: break;
  }
  if (r.mode == RecMode::Memo) return memo_hint(r);
  // docs/13 §5.6; docs/20 U23: Space stars too, [G0]/[Space]star (1-9 is on the Tab menu,
  // and the title shows where it goes)
  return tr(txt::kMeetingHint);
}

}  // namespace cpa
