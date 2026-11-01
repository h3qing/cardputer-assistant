// Shared words (docs/21 §6.2, docs/20 U22 "one action, one name"): the home tiles, the
// Tab menu and the hint bars name a thing with the same constant. English is in appendix
// A's wording; hint-bar words are lower case ("[Tab]menu").
#pragma once

#include "txt.h"

namespace cpa {
namespace txt {

constexpr TileTxt kSettings{"设置", "Settings"};
constexpr TileTxt kHome{"主页", "Home"};
constexpr TileTxt kProjects{"项目", "Projects"};
constexpr TileTxt kWaiting{"等你", "Inbox"};  // the place; a count says "waiting" (docs/21 §6.7)
constexpr TileTxt kMeeting{"开会", "Meeting"};
constexpr TileTxt kNote{"记一条", "Note"};
constexpr TileTxt kAiQueue{"排队给 AI", "AI queue"};
constexpr MenuTxt kNewSession{"开 session", "New session"};  // the tile says "Session"
constexpr TileTxt kReply{"回复", "Reply"};
constexpr TileTxt kLock{"锁屏", "Lock"};
constexpr TileTxt kVoiceMemo{"语音便条", "Voice memo"};
constexpr TileTxt kDevice{"设备", "Device"};
// A count on a tile or a menu row: sessions running (the "!N" for those waiting is the same
// in both languages).
constexpr Txt kRunning{"跑%lu", "run%lu"};
// What home, the inbox and the board say about the Mac's picture of the sessions
// (docs/20 U19), each a line in a card or beside Mando.
constexpr Txt kAllDone{"都处理完了", "All done"};
constexpr Txt kNoneNeedsYou{"没有等你的 session", "No session needs you"};
constexpr Txt kNoMacYet{"还没连上 Mac", "No Mac yet"};
constexpr Txt kNoCpad{"Mac 上 cpad 没在跑", "cpad isn't running on the Mac"};
constexpr Txt kCheckCpad{"查一下：cpa status", "Check: cpa status"};
constexpr Txt kVersionsDiffer{"固件和 Mac 端版本不兼容", "Firmware and Mac don't match"};
constexpr Txt kPleaseUpdate{"请更新", "Please update"};
constexpr Txt kOptDigitsRow{"第 N 个项目页", "Project N", kMenuTxtW};
constexpr Txt kUp{"上移", "Up", kMenuTxtW};
constexpr Txt kDown{"下移", "Down", kMenuTxtW};
constexpr Txt kBackRow{"返回", "Back", kMenuTxtW};
constexpr Txt kReplyRow{"回复选中的 session", "Reply to session", kMenuTxtW};
constexpr Txt kDismissRow{"忽略（在线时）", "Dismiss (online)", kMenuTxtW};
constexpr Txt kDismissed{"已忽略：", "Dismissed: "};

// The days, Sunday first (the status bar, home, the lock card): English is narrower.
constexpr Txt kWeekdays[] = {{"周日", "Sun"}, {"周一", "Mon"}, {"周二", "Tue"}, {"周三", "Wed"},
                             {"周四", "Thu"}, {"周五", "Fri"}, {"周六", "Sat"}};

// "● 准备": a hold is about to record a memo, the microphone is not up yet.
constexpr Txt kMemoReady{"\xE2\x97\x8F 准备", "\xE2\x97\x8F Wait"};
// The battery as a word and a percentage (the meeting page, the lock card).
constexpr Txt kBatNone{"电 --", "Bat --"};
constexpr Txt kBatPct{"电 %d%%", "Bat %d%%"};
constexpr Txt kMenu{"菜单", "menu", kKeyTxtW};  // the hint bar's last item, after [Tab]
constexpr Txt kBack{"返回", "back", kKeyTxtW};
constexpr Txt kPick{"选", "pick", kKeyTxtW};
constexpr Txt kChange{"改", "change", kKeyTxtW};

}  // namespace txt
}  // namespace cpa
