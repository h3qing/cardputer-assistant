// expect: ok
// The good cases: each class at its limit, formats, the narrow symbols, arrays and counts.
#include "lang_names.h"
#include "txt.h"
#include "words.h"

[[maybe_unused]] constexpr cpa::Txt kQueueEmpty{"队列为空", "Queue is empty"};
[[maybe_unused]] constexpr cpa::ToastTxt kOther{"其他项目只在 Mac 上看", "Other projects: see the Mac"};
[[maybe_unused]] constexpr cpa::Txt kSwitched{"%s · 再按 Enter 换回 %s", "%s · Enter again: %s"};
[[maybe_unused]] constexpr cpa::LineTxt kWifi{"无 · 只用 USB 线", "None · USB only"};
[[maybe_unused]] constexpr cpa::SignTxt kSign{"保存中…", "Saving…"};
[[maybe_unused]] constexpr cpa::TileTxt kTiles[2] = {{"项目", "Projects"}, {"等你", "Waiting"}};
[[maybe_unused]] constexpr cpa::CountTxt kNotes{"%u 条笔记等着传到 Mac", "%u note waits for the Mac", "%u notes wait for the Mac"};
[[maybe_unused]] constexpr cpa::Txt kPercent{"电量 %u%%", "Bat %u%%"};
[[maybe_unused]] constexpr cpa::Txt kArrows{"↑0 录2", "↑0 R2"};

int main() { return kQueueEmpty.zh[0] == 0; }
