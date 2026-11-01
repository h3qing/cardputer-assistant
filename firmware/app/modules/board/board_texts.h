// The words of 项目 / Projects (docs/21 §6.7, appendix A): the overview, a project's
// Sessions and queue tabs, their Tab menu rows, hints and toasts, each a checked zh/en pair
// (txt.h). Hint strings bracket their keys in English (hint_layout.h rule 6).
#pragma once

#include "txt.h"
#include "words.h"

namespace cpa {
namespace txt {

// --- the Tab menu's 本页 rows ---
constexpr MenuTxt kOpenProject{"打开选中的项目", "Open this project"};
constexpr MenuTxt kToQueue{"切到队列", "To the queue"};
constexpr MenuTxt kSpawnAsks{"开 session（先确认）", "New session (asks)"};
constexpr MenuTxt kMarkDone{"标记完成（在线时）", "Mark done (online)"};
constexpr MenuTxt kToSessions{"切到 Sessions", "To Sessions"};

// --- a project card's counts: running, waiting for you, queued, in all ---
constexpr Txt kRunCount{"跑", "run"};
constexpr Txt kNeedCount{"等", "!"};
constexpr Txt kQueueCount{"队", "Q"};
constexpr Txt kAllCount{"共", "all"};
constexpr Txt kOther{"其他", "Other"};
constexpr CountTxt kOtherSessions{"%u 个 session 没归项目", "%u with no project", "%u with no project"};

// --- the title line, the lists ---
constexpr Txt kUpdate{"固件和 Mac 端版本不兼容，请更新", "Firmware and Mac don't match: update"};
constexpr Txt kNoAgentLine{"Mac 上未找到 Claude Code", "No Claude Code on the Mac"};
constexpr Txt kUpdatedAgo{"更新于 %s前", "Updated %s ago"};
constexpr LineTxt kNoProjectsYet{"还没有项目。在 Mac 上运行：", "No projects yet. On the Mac, run:"};
constexpr Txt kQueueTab{"队列(%u)", "Queue(%u)"};
constexpr LineTxt kNoSessions{"没有 session", "No sessions"};
constexpr LineTxt kQueueEmpty{"队列为空", "Queue is empty"};
constexpr int16_t kMarkTxtW = 24;  // board_module.cpp kMarkWEn less 6 px
constexpr Txt kSpawnMark{"开", "new", kMarkTxtW};
constexpr Txt kReplyMark{"回", "rep", kMarkTxtW};
constexpr Txt kUnsureMark{"开?", "new?", kMarkTxtW};
constexpr Txt kOfflinePill{"离线", "offline"};

// --- hints and toasts ---
constexpr Txt kOverviewHint{"Enter打开 Opt+1-9项目 `返回  Tab菜单", "[Enter]open [Opt+1-9]jump [`]back [Tab]menu"};
constexpr Txt kQueueHint{"Enter开session `返回  Tab菜单", "[Enter]new session [`]back [Tab]menu"};
constexpr ToastTxt kOfflineNotNow{"离线，此操作不可用", "Offline: not available"};
constexpr ToastTxt kNoAgentToast{"Mac 上未找到 Claude Code", "No Claude Code on the Mac"};
constexpr ToastTxt kOtherOnMac{"其他项目只在 Mac 上看", "Other projects: see the Mac"};
constexpr ToastTxt kTryLaterBusy{"请稍后再试（已有请求在途）", "Try again soon (a request is pending)"};
constexpr Txt kDone{"已完成：", "Done: "};

}  // namespace txt
}  // namespace cpa
