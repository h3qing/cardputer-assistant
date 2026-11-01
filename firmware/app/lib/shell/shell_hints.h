// The shell's hint strings (docs/21 §6.6): the editor's and the recording pages' bars and
// the keys lines on their cards, a zh/en pair each. English brackets its keys
// ("[Enter]save [`]exit", hint_layout.h rule 6), which the Txt width check would measure
// with the brackets, so test_hint_bars draws each in both languages instead.
#pragma once

#include "txt.h"

namespace cpa {
namespace txt {

constexpr Txt kDiscardKeys{"Enter 放弃    ` 返回编辑", "[Enter]discard    [`]keep editing"};
constexpr Txt kDiscardHint{"Enter 放弃  ` 返回", "[Enter]discard  [`]back"};
constexpr Txt kEmptyHint{"退格 退出  Opt N/Q/S 类型  Tab菜单", "[Del]exit  [Opt+N/Q/S]type  [Tab]menu"};
constexpr Txt kTextHint{"Enter 保存  ` 退出  Tab菜单", "[Enter]save  [`]exit  [Tab]menu"};
constexpr Txt kMemoLatchedHint{"G0结束 1-9选项目 Opt+L锁屏", "[G0]stop [1-9]project [Opt+L]lock"};
constexpr Txt kMemoGraceHint{"G0按回继续 1-9选项目", "[G0]hold again [1-9]project"};
constexpr Txt kMemoHint{"G0松开结束 1-9选项目", "[G0]let go: stop [1-9]project"};
constexpr Txt kLetGoG0{"松开 G0", "Let go of [G0]"};
constexpr Txt kStartBoxHint{"Opt+L 取消并锁屏  Tab菜单", "[Opt+L]cancel, lock  [Tab]menu"};
constexpr Txt kEndBoxHint{"Opt+L 继续录并锁屏  Tab菜单", "[Opt+L]keep going, lock  [Tab]menu"};
constexpr Txt kMeetingHint{"G0 空格 加★  Opt+R结束  Tab菜单", "[G0]/[Space]star [Opt+R]end [Tab]menu"};

}  // namespace txt
}  // namespace cpa
