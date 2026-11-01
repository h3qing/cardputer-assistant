#include "rec_result.h"

#include <cstdio>

#include "rec_overlay.h"
#include "txt.h"

namespace cpa {
namespace {

// docs/21: the words of a recording's end; "<what>: <why> · saved <len>" is whole in the
// 224 px toast with the longest reason and a 3-hour length (test_text_width).
constexpr Txt kMeeting{"会议", "Meeting"};
constexpr Txt kMemo{"便条", "Memo"};
constexpr Txt kLowBattery{"电量低", "low battery"};
constexpr Txt kBacklog{"待传录音太多", "sync backlog"};
constexpr Txt kSdFull{"SD 卡满", "SD full"};
constexpr Txt kStopped{"已停", "stopped"};
constexpr ToastTxt kTooShort{"录音太短，已丢弃", "Too short: discarded"};
constexpr Txt kSavedLen{"录音已保存 · %s", "Recording saved · %s"};
constexpr Txt kCapSaved{"已到%s上限 · 已存 %s", "%s limit reached · saved %s"};
constexpr Txt kCap3h{" 3 小时", "3 h"};
constexpr Txt kCap5min{" 5 分钟", "5 min"};
constexpr Txt kStoppedSaved{"%s已停：%s · 已存 %s", "%s: %s · saved %s"};
constexpr Txt kStoppedLost{"%s已停：%s（太短，未存）", "%s: %s · not saved"};
constexpr ToastTxt kHeldCap{"已到 5 分钟，已保存", "5 min reached: saved"};
constexpr ToastTxt kHeldSaved{"录音已停，已保存", "Recording stopped: saved"};
constexpr ToastTxt kHeldStopped{"录音已停", "Recording stopped"};
constexpr ToastTxt kHeldTip{"要录更久：松开 G0，再按 Opt+R 开会", "Longer? Let go, then Opt+R"};
constexpr ToastTxt kBatWarn{"电量低：录音可能很快自动停止", "Battery low: recording may stop soon"};
constexpr LineTxt kCap3hCard{"已到 3 小时上限", "3 h limit reached"};
constexpr LineTxt kCap5minCard{"已到 5 分钟上限", "5 min limit reached"};
constexpr LineTxt kPartsOnCard{"已写下的段还在卡上", "Parts so far are on the card"};
constexpr LineTxt kNeedsSd{"需要 SD 卡（插卡后重启）", "Needs an SD card (insert, restart)"};
constexpr LineTxt kNoMemory{"内存不足", "Out of memory"};
constexpr LineTxt kNvsError{"NVS 错误", "NVS error"};
constexpr LineTxt kCardError{"SD 卡出错", "SD card error"};
constexpr LineTxt kCardTooMany{"卡上段太多，先同步", "Too many files: sync first"};
constexpr Txt kWhatStopped{"%s已停：%s", "%s: %s"};  // English: the lock card's 145 px column
constexpr Txt kCardSaved{"\xE2\x88\x9A 已存 %s", "\xE2\x88\x9A Saved %s"};  // √
constexpr LineTxt kCardTimeout{"\xC3\x97 收尾超时", "\xC3\x97 didn't close"};  // ×
constexpr LineTxt kCardStopped{"\xC3\x97 录音已停", "\xC3\x97 stopped"};
constexpr LineTxt kCardRefused{"\xC3\x97 没录上", "\xC3\x97 not recorded"};
constexpr LineTxt kCardShort{"\xC3\x97 太短，未存", "\xC3\x97 Too short"};

const char* what(RecMode m) { return tr(m == RecMode::Meeting ? kMeeting : kMemo); }

// Why it stopped by itself, as the end line names it.
const char* stop_reason(rec::RecRefusal why) {
  switch (why) {
    case rec::RecRefusal::Battery: return tr(kLowBattery);
    case rec::RecRefusal::Pending: return tr(kBacklog);
    case rec::RecRefusal::CardFull: return tr(kSdFull);
    default: return tr(kStopped);
  }
}

}  // namespace

RecText rec_end_text(const rec::RecEffect& e) {
  const FixedStr<16> len = rec_clock_text(e.ms);
  char buf[96];
  if (e.why == rec::RecRefusal::None) {
    if (!e.saved) return RecText::from(tr(kTooShort));
    std::snprintf(buf, sizeof buf, tr(kSavedLen), len.c_str());
  } else if (e.why == rec::RecRefusal::Cap) {
    std::snprintf(buf, sizeof buf, tr(kCapSaved), tr(e.mode == RecMode::Meeting ? kCap3h : kCap5min), len.c_str());
  } else if (e.saved) {
    std::snprintf(buf, sizeof buf, tr(kStoppedSaved), what(e.mode), stop_reason(e.why), len.c_str());
  } else {
    std::snprintf(buf, sizeof buf, tr(kStoppedLost), what(e.mode), stop_reason(e.why));
  }
  return RecText::from(buf);
}

ToastLevel rec_end_level(const rec::RecEffect& e) {
  if (e.why != rec::RecRefusal::None) return ToastLevel::Warn;
  return e.saved ? ToastLevel::Ok : ToastLevel::Info;
}

const char* rec_held_text(const RecInfo& r) {
  if (r.last.has && r.last.why == static_cast<uint8_t>(rec::RecRefusal::Cap)) return tr(kHeldCap);
  if (r.last.has && r.last.saved) return tr(kHeldSaved);
  return tr(kHeldStopped);
}

// In order: while G0 is held every key but its release is ignored, Opt+R included.
const char* rec_held_tip(const RecInfo& r) {
  const bool cap = r.last.has && r.last.why == static_cast<uint8_t>(rec::RecRefusal::Cap);
  return cap ? tr(kHeldTip) : "";
}

const char* rec_bat_warn_text() { return tr(kBatWarn); }

namespace {

bool refused(const RecLast& l) { return !l.saved && l.ms == 0 && l.why != 0; }

// Why it did not start, or why it stopped by itself, as the card's second line.
RecText card_detail(const RecLast& l) {
  const rec::RecRefusal why = static_cast<rec::RecRefusal>(l.why);
  switch (why) {
    case rec::RecRefusal::None: return RecText{};
    case rec::RecRefusal::Cap: return RecText::from(tr(l.mode == RecMode::Meeting ? kCap3hCard : kCap5minCard));
    case rec::RecRefusal::Timeout: return RecText::from(tr(kPartsOnCard));
    case rec::RecRefusal::NoSd: return RecText::from(tr(kNeedsSd));
    case rec::RecRefusal::NoMemory: return RecText::from(tr(kNoMemory));
    case rec::RecRefusal::NoIds: return RecText::from(tr(kNvsError));
    case rec::RecRefusal::CardError: return RecText::from(tr(kCardError));  // a Refuse: the card failed
    case rec::RecRefusal::Incomplete: return RecText::from(tr(kCardTooMany));  // docs/28 D15
    default: break;  // Battery, Pending, CardFull: either
  }
  if (refused(l)) return RecText::from(stop_reason(why));
  char buf[64];
  std::snprintf(buf, sizeof buf, tr(kWhatStopped), what(l.mode), stop_reason(why));
  return RecText::from(buf);
}

}  // namespace

CardResult rec_card_result(const RecLast& l) {
  CardResult r;
  r.saved = l.saved;
  r.started = !refused(l);
  char buf[48];
  if (l.saved) {
    std::snprintf(buf, sizeof buf, tr(kCardSaved), rec_clock_text(l.ms).c_str());
    r.line = RecText::from(buf);
  } else if (l.why == static_cast<uint8_t>(rec::RecRefusal::Timeout)) {
    r.line = RecText::from(tr(kCardTimeout));
  } else if (l.why == static_cast<uint8_t>(rec::RecRefusal::CardError)) {
    r.line = RecText::from(tr(kCardStopped));  // mid-way: what was written is on the card
  } else {
    r.line = RecText::from(tr(refused(l) ? kCardRefused : kCardShort));
  }
  r.detail = card_detail(l);
  return r;
}

RecLast rec_last_of(const rec::RecEffect& e, bool locked) {
  RecLast l;
  l.has = true;
  l.locked = locked;
  l.saved = e.kind == rec::RecFx::Ended && e.saved;
  l.why = static_cast<uint8_t>(e.why);
  l.mode = e.mode;
  l.ms = e.kind == rec::RecFx::Ended ? e.ms : 0;
  return l;
}

}  // namespace cpa
