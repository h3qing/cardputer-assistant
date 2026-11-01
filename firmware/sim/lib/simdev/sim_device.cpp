#include "sim_device.h"

#include <algorithm>
#include <cmath>

#include "module_list.h"

namespace cpasim {
namespace {

constexpr uint32_t kCpafsBytes = 0x180000;  // partitions/cpa_8MB.csv
constexpr uint32_t kCardMib = 61035;        // a 64 GB card (64e9 bytes)
constexpr uint64_t kCardFree = static_cast<uint64_t>(kCardMib - 16) << 20;  // empty: all but the FAT
constexpr double kToneHz = 440.0;
constexpr double kRate = 16000.0;
constexpr double kTwoPi = 6.283185307179586;
// hal/hal_platform.h (device only: it needs M5GFX): the debug.reboot ack leaves first.
constexpr uint32_t kRebootDelayMs = 200;

cpa::BootTicket ticket(uint32_t boot) {
  const cpa::BootRecord rec{0x51A1D00Du, boot};
  return cpa::boot_ticket(rec, rec).value;
}

cpa::BootInfo boot_info(uint32_t boot) {
  cpa::BootInfo b;
  b.ticket = ticket(boot);
  b.dev = cpa::FixedStr<12>::from("100000000051");
  b.rst = cpa::FixedStr<16>::from(boot == 1 ? "poweron" : "software");
  return b;
}

// What hal_part_facts(), hal_sd_mount() and LittleFS report, planned as setup() does.
cpa::StorageBoot storage_of(cpa::Store s) {
  cpa::StorageBoot sb;
  sb.part.cpafs_bytes = kCpafsBytes;
  sb.sd = s == cpa::Store::Sd ? cpa::SdFacts{cpa::SdState::Ok, kCardMib} : cpa::SdFacts{cpa::SdState::Absent, 0};
  sb.plan = cpa::plan_storage(sb.part, sb.sd, s != cpa::Store::None);
  return sb;
}

}  // namespace

SimDevice::SimDevice(const BootSpec& boot, cpa::Transport* link)
    : spec_(boot), link_(link ? *link : static_cast<cpa::Transport&>(tx_)), port_(new SimRecPort) {
  tx_.is_plugged = boot.usb;
  plat_.next.bat_mv = boot.bat_mv;
  plat_.next.heap_free = 180 * 1024;
  plat_.next.heap_min = 150 * 1024;
  plat_.next.heap_max_block = 96 * 1024;
  plat_.next.stack_hwm = 9 * 1024;
  store_.set_fat(boot.store == cpa::Store::Sd);
  plat_.nvs = cpa::with_lang(plat_.nvs, boot.lang);  // docs/21: what NVS holds at power-on
  start();
}

void SimDevice::start() {
  cpa::StorageBoot sb = storage_of(spec_.store);
  sb.migrate_from = sb.plan.migrate ? &cpafs_ : nullptr;  // no observer: the "i/n" screen is not drawn
  const bool sd = sb.plan.primary == cpa::Store::Sd;
  cpa::FileStore& fs = sb.plan.primary == cpa::Store::None ? static_cast<cpa::FileStore&>(null_) : store_;
  rt_.reset(new cpa::Runtime(link_, fs, plat_, disp_, cpa::kModules, cpa::kModuleCount));
  cpa::RecBoot rb;
  rb.buffers = sd;
  rb.free_known = sd;
  rb.free_bytes = sd ? kCardFree : 0;
  rt_->attach_rec(*port_, rb);
  rt_->begin(boot_info(boot_), now_, sb);
  port_->boot(rt_->ids());
}

// The card and the outbox store keep what was durable; the audio task restarts on them.
void SimDevice::reboot() {
  rt_.reset();
  store_.power_cycle();
  cpafs_.power_cycle();
  std::unique_ptr<SimRecPort> fresh(new SimRecPort);
  fresh->fs = port_->fs;
  fresh->fs.reboot();
  port_ = std::move(fresh);
  tx_.rx.clear();
  ++boot_;
  now_ = 0;
  reboot_at_ = 0;
  start();
}

void SimDevice::watch_reboot() {
  if (plat_.reboots == reboots_seen_) return;
  reboots_seen_ = plat_.reboots;
  reboot_at_ = now_ + kRebootDelayMs;
}

void SimDevice::loop(cpa::KeySet keys) {
  now_ += 10;
  port_->now = now_;
  rt_->loop(now_, keys);
  watch_reboot();
}

void SimDevice::host(const std::string& line) { tx_.host(line); }

void SimDevice::mic_block(uint32_t level) {
  if (!recording()) return;
  std::string pcm(kMicBlockBytes, '\0');
  const double amp = std::min(32767.0, level * std::sqrt(2.0));
  for (size_t i = 0; i < kMicBlockBytes / 2; ++i, ++samples_) {
    const auto v = static_cast<int16_t>(std::lround(amp * std::sin(kTwoPi * kToneHz * samples_ / kRate)));
    pcm[2 * i] = static_cast<char>(v & 0xFF);
    pcm[2 * i + 1] = static_cast<char>((v >> 8) & 0xFF);
  }
  mic_pcm(pcm);
}

void SimDevice::mic_pcm(const std::string& pcm) {
  if (!recording()) return;
  port_->now = now_ - kMicBlockMs;  // feed_pcm() adds the block's 128 ms
  port_->feed_pcm(pcm);
}

std::vector<std::string> SimDevice::dels_for_last_items() {
  std::vector<std::string> out;
  const auto pages = tx_.of_type("file.items");
  if (pages.empty()) return out;
  const auto& items = std::get<cpa::proto::FileItems>(pages.back().msg.body);
  for (uint8_t i = 0; i < items.n; ++i) {
    const cpa::proto::FileItem& it = items.items[i];
    std::string f = "\"fid\":\"" + std::string(it.id.c_str()) + "\",\"size\":" + std::to_string(it.size);
    if (it.sha256.has) f += ",\"sha256\":\"" + std::string(it.sha256.value.c_str()) + "\"";
    out.push_back(f);
  }
  return out;
}

std::string SimDevice::last_sent_id(const std::string& type) {
  const auto sent = tx_.of_type(type.c_str());
  return sent.empty() ? std::string() : std::string(sent.back().msg.id.c_str());
}

// gh #51: a G0 tap's note opens kDoubleTapMs after it (g0_gesture.h): a shot waits for it.
bool SimDevice::idle(bool still) const {
  const cpa::Shell& sh = rt_->shell();
  return tx_.rx.empty() && !sh.dirty() && !sh.g0_waiting() && (!still || !sh.sliding());
}

SimDevice::Edges SimDevice::edges() const {
  Frame f(cpa::kScreenW, cpa::kScreenH);
  FbCanvas c(f);
  rt_->shell().render(c, now_);
  return Edges{c.offscreen_texts(), c.last_offscreen()};
}

cpa::Backlight SimDevice::backlight() const {
  return plat_.levels.empty() ? cpa::Backlight::On : plat_.levels.back();
}

}  // namespace cpasim
