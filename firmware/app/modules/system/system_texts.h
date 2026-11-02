// The words of Settings (docs/21 §4, appendix A): the settings page, the WiFi page
// and the device page, each a zh/en pair checked when it is built (txt.h). Strings with a
// % skip the width check; test_mod_system draws them with their widest arguments.
#pragma once

#include "txt.h"
#include "words.h"

namespace cpa {
namespace txt {

// A WiFi page value: x 36 (zh) or 42 (en, after "Later") to x 236.
constexpr int16_t kWifiValueW = 194;
// A settings row's value: x 100 to x 236.
constexpr int16_t kSettingValueW = 136;

// --- the settings page (§4.2) ---
constexpr Txt kWifiOff{"无 · 只用 USB 线", "None · USB only", kSettingValueW};
constexpr Txt kLanguage{"语言", "Language"};  // drawn with both(): "语言 Language"
constexpr Txt kLink{"连接", "Link", 30};
constexpr Txt kSync{"同步", "Sync", 30};
constexpr Txt kOverUsb{"USB 线 · ", "USB · "};
constexpr Txt kOnline{"在线", "online"};
constexpr Txt kOffline{"离线", "offline"};
constexpr Txt kNever{"从未", "never"};
constexpr Txt kAgo{"%s前", "%s ago"};
constexpr Txt kToSync{"%s · 待传 ↑%u", "%s · to sync ↑%u"};
constexpr Txt kRecsToSync{" 录%u", " R%u"};
constexpr Txt kKeptToSync{" 留%u", " kept %u"};  // docs/28: kept copies

// --- the storage tab (docs/28 S1a, §3.3, §3.5) ---
constexpr TileTxt kStorage{"存储", "Storage"};
constexpr Txt kAgainRow{"再交一次", "Send again", 90};
constexpr Txt kClearRow{"清掉留底", "Clear kept", 90};
constexpr Txt kGiveRow{"给这台 Mac", "To this Mac", 90};
constexpr Txt kAgainValue{"%s 的 %u 次", "from %s: %u"};  // the Mac that will not get them
constexpr CountTxt kSegsValue{"%u 段", "%u file", "%u files"};
constexpr Txt kNoneValue{"没有", "none", kSettingValueW};
constexpr CountTxt kKeptFact{"留底 %u 段", "Kept %u file", "Kept %u files"};
constexpr Txt kOldestDays{" · 最旧 %u 天", " · oldest %u d"};
constexpr Txt kWaitingFact{"待传 %u · 空闲 %s", "To sync %u · free %s"};
constexpr LineTxt kPlainNote{"留底未加密：去别处前清掉", "Kept copies are plain: clear first"};
constexpr CountTxt kClearAsk{"清掉 %u 段留底？", "Clear %u kept file?", "Clear %u kept files?"};
constexpr LineTxt kClearSub{"Mac 上已存的不受影响", "What the Macs have stays"};
constexpr Txt kAgainAsk{"把 %u 次再交给别的 Mac？", "Send %u to another Mac?", 200};
constexpr Txt kAgainSub{"%s 不会再拿到", "%s won't get them again", 200};
constexpr Txt kAgainBtn{"再交", "Send", 60};
constexpr Txt kCancelBtn{"取消", "Cancel", 60};
constexpr Txt kClearBtn{"清掉", "Clear", 60};
constexpr ToastTxt kNoKept{"没有留底", "No kept copies"};
constexpr ToastTxt kNothingAgain{"没有可再交的", "Nothing to send again"};
constexpr ToastTxt kNothingHeld{"没有扣下不给这台 Mac 的", "Nothing kept from this Mac"};
constexpr ToastTxt kWorking{"正在处理，稍等", "Working: a moment"};
constexpr MenuTxt kDoRow{"做这一行", "Do this row"};
constexpr MenuTxt kClearYes{"清掉", "Clear"};
constexpr MenuTxt kClearNo{"取消", "Cancel"};
constexpr MenuTxt kOtherButton{"换按钮", "Other button"};
constexpr Txt kDo{"做", "do", kKeyTxtW};

// --- the WiFi page (§4.4, W0) ---
constexpr Txt kNoneTag{"无", "None"};
constexpr Txt kNow{"现在", "Now"};
constexpr Txt kData{"数据", "Data"};
constexpr Txt kWant{"想用", "Want"};
constexpr Txt kMind{"注意", "Note"};
constexpr Txt kLater{"以后", "Later"};
constexpr Txt kNoWifi{"这个固件不带 WiFi", "No WiFi in this firmware", kWifiValueW};
constexpr Txt kUsbOnly{"只用 USB 线连你的 Mac，不上网", "USB to your Mac, never online", kWifiValueW};
constexpr Txt kFactory{"出厂固件有 WiFi：", "Factory firmware has WiFi:", kWifiValueW};
constexpr Txt kViaLauncher{"关开电源进 Launcher 切过去", "power-cycle into the Launcher", kWifiValueW};
constexpr Txt kNeedsLauncher{"先装 Launcher（见 README）", "Needs the Launcher (README)", kWifiValueW};
constexpr Txt kPrintsPasswords{"它会把 WiFi 密码明文打到串口", "Prints WiFi passwords to serial", kWifiValueW};
constexpr Txt kNotPlanned{"还没排期", "not planned yet", kWifiValueW};

// --- the device page (§4.5): every label one word, 36 px at most ---
constexpr Txt kFwRow{"固件 %s  rst %s", "FW %s  rst %s"};
constexpr Txt kBootRow{"启动 %s  daemon %s", "Boot %s  daemon %s"};
constexpr Txt kDirect{"直接", "direct"};
constexpr Txt kStore{"存储 ", "Store "};
constexpr Txt kInternal{"内部", "Flash"};
constexpr Txt kNoStore{"无", "none"};
constexpr Txt kFree{"剩 ", "free "};
constexpr Txt kMoved{"已迁移 ", "moved "};
constexpr Txt kMoveFailed{"迁移失败 ", "failed "};
constexpr Txt kOutboxRow{"outbox 待发 %u 在途 %u 坏 %u%s", "outbox pending %u out %u bad %u%s"};
constexpr Txt kOutboxFull{" 已满", " full"};
constexpr Txt kHeapRow{"堆 %s 最小 %s 块 %s", "Heap %s min %s blk %s"};
constexpr Txt kBatRow{"电池 %umV  栈余 %s", "Bat %umV  stack %s"};
// the warning row's pieces, most urgent first
constexpr Txt kStoreErrors{"存储错误 %u，请重启", "Store errors %u: restart"};
constexpr LineTxt kNvsError{"NVS 错误：不能保存", "NVS error: cannot save"};
constexpr Txt kInvariant{"不变式被打破 %u", "Invariant broken %u"};
constexpr Txt kDisabledModules{"禁用模块 %u", "Modules off %u"};
constexpr Txt kKeyboard{"键盘 幽灵 %u 重置 %u", "Keys ghost %u reset %u"};
constexpr LineTxt kNoCard{"SD：未检测到卡", "SD: no card"};
constexpr LineTxt kSdUnusable{"SD 卡不能用：在 Mac 上格式化成 FAT32", "SD unusable: format FAT32 on a Mac"};
constexpr LineTxt kNoInternal{"无内部存储，只用 SD", "No flash store: SD only"};
constexpr LineTxt kLegacyTable{"分区表是 v0.1 的，请整片刷写", "v0.1 partition table: flash it all"};
constexpr LineTxt kNoStorage{"没有可用存储：请插入 FAT32 SD 卡", "No storage: insert a FAT32 SD card"};

// --- the Tab menu's "Here" rows and the hint bar (§4.6) ---
constexpr MenuTxt kChangeRow{"改这一行", "Change this row"};
constexpr MenuTxt kPrevTab{"上一个标签", "Previous tab"};
constexpr MenuTxt kNextTab{"下一个标签", "Next tab"};
constexpr MenuTxt kBackToSettings{"回到设置", "Back to settings"};
constexpr Txt kSwitchPage{"换页", "page", kKeyTxtW};

}  // namespace txt
}  // namespace cpa
