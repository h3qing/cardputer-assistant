// The device side of the fuzz test: the Runtime wired as lib/simdev/sim_device.cpp does
// (same fakes, same storage plan, SimRecPort), but reachable so the test can look at it
// after every 10 ms loop.
#include <algorithm>
#include <cmath>

#include "fuzz.h"
#include "module_list.h"
#include "storage_plan.h"

namespace fuzz {
namespace {

constexpr uint32_t kCardMib = 61035;

cpa::BootInfo boot_info(uint32_t boot) {
  const cpa::BootRecord rec{0x51A1D00Du, boot};
  cpa::BootInfo b;
  b.ticket = cpa::boot_ticket(rec, rec).value;
  b.dev = cpa::FixedStr<12>::from("100000000051");
  b.rst = cpa::FixedStr<16>::from(boot == 1 ? "poweron" : "software");
  return b;
}

cpa::StorageBoot storage_of(cpa::Store s) {
  cpa::StorageBoot sb;
  sb.part.cpafs_bytes = 0x180000;
  sb.sd = s == cpa::Store::Sd ? cpa::SdFacts{cpa::SdState::Ok, kCardMib} : cpa::SdFacts{cpa::SdState::Absent, 0};
  sb.plan = cpa::plan_storage(sb.part, sb.sd, s != cpa::Store::None);
  return sb;
}

}  // namespace

bool Port::post(const cpa::rec::RecCmd& c) {
  if (c.kind == cpa::rec::CmdKind::Start && late_held_) late_due_ = true;  // out before the Start's events
  return SimRecPort::post(c);
}

bool Port::poll(cpa::rec::RecEvt& e) {
  if (late_due_) {
    late_due_ = late_held_ = false;
    e = late_;
    return true;
  }
  for (;;) {
    if (!SimRecPort::poll(e)) return false;
    const bool end = e.kind == cpa::rec::EvtKind::Saved || e.kind == cpa::rec::EvtKind::Discarded ||
                     e.kind == cpa::rec::EvtKind::Failed;
    if (end && hold_end) {
      hold_end = false;
      late_ = e;
      late_held_ = true;
      continue;
    }
    if (!end || drop_end <= 0) return true;
    --drop_end;  // lost on the way from the audio task (docs/20 S1)
  }
}

Dev::Dev(cpa::Store k, cpa::Lang lang) : kind(k), port(new Port) {
  plat.nvs = cpa::with_lang(plat.nvs, lang);
  tx.is_plugged = true;
  plat.next.bat_mv = 4100;
  plat.next.heap_free = 180 * 1024;
  plat.next.heap_min = 150 * 1024;
  plat.next.heap_max_block = 96 * 1024;
  plat.next.stack_hwm = 9 * 1024;
  store.set_fat(k == cpa::Store::Sd);
  start();
}

void Dev::start() {
  cpa::StorageBoot sb = storage_of(kind);
  sb.migrate_from = sb.plan.migrate ? &cpafs : nullptr;
  const bool sd = sb.plan.primary == cpa::Store::Sd;
  cpa::FileStore& fs = sb.plan.primary == cpa::Store::None ? static_cast<cpa::FileStore&>(nul) : store;
  rt.reset(new cpa::Runtime(tx, fs, plat, disp, cpa::kModules, cpa::kModuleCount));
  cpa::RecBoot rb;
  rb.buffers = sd;
  rb.free_known = sd;
  rb.free_bytes = sd ? (static_cast<uint64_t>(kCardMib - 16) << 20) : 0;
  rt->attach_rec(*port, rb);
  rt->begin(boot_info(boot), now, sb);
  port->boot(rt->ids());
}

void Dev::reboot() {
  rt.reset();
  store.power_cycle();
  cpafs.power_cycle();
  std::unique_ptr<Port> fresh(new Port);
  fresh->fs = port->fs;
  fresh->fs.reboot();
  port = std::move(fresh);
  tx.rx.clear();
  ++boot;
  now = 0;
  reboot_at = 0;
  start();
}

void Dev::loop(cpa::KeySet keys) {
  now += kTick;
  port->now = now;
  rt->loop(now, keys);
  if (plat.reboots == reboots_seen) return;
  reboots_seen = plat.reboots;
  reboot_at = now + 200;  // hal_platform: the debug.reboot ack leaves first
}

void Dev::mic_block(uint32_t level) {
  if (!recording()) return;
  std::string pcm(4096, '\0');
  const double amp = std::min(32767.0, level * 1.41421356);
  for (size_t i = 0; i < 2048; ++i, ++samples) {
    const auto v = static_cast<int16_t>(std::lround(amp * std::sin(6.283185307179586 * 440.0 * samples / 16000.0)));
    pcm[2 * i] = static_cast<char>(v & 0xFF);
    pcm[2 * i + 1] = static_cast<char>((v >> 8) & 0xFF);
  }
  port->now = now - 128;
  port->feed_pcm(pcm);
}

}  // namespace fuzz
