// SimDevice (gh #32): the device's runtime on the host. The only place that wires the
// Runtime: the real modules (src/module_list.cpp) on the native test rig's fakes, with
// SimDisplay for the screen, the audio task as SimRecPort (a real RecEngine on a fake
// card), and time advanced by the caller in 10 ms loops. Wiring changes in later
// firmware (new ports, new constructor arguments) only touch sim_device.cpp.
// Storage is planned as setup() plans it (cpa::plan_storage) from the facts of a full
// flash of partitions/cpa_8MB.csv: store=sd is an empty 64 GB card with cpafs mounted
// (the boot migration runs, from an empty cpafs), internal is cpafs with no card
// (StoreNote::NoCard), none is neither.
// The USB link is the scripts' FakeTransport, or the caller's (serve: a pty). debug.reboot
// restarts the runtime 200 ms after its ack, as hal_platform does: the card, the outbox
// store and the boot counter survive it, RAM (the runtime, the audio task) does not.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/support/fake_store.h"
#include "key_set.h"
#include "null_store.h"
#include "runtime.h"
#include "script.h"
#include "sim_display.h"
#include "support/fake_io.h"
#include "sim_rec_port.h"

namespace cpasim {

// The audio task's block (hal/audio_io.h, device only): 2048 samples, 16 kHz mono s16le.
constexpr size_t kMicBlockBytes = 4096;
constexpr uint32_t kMicBlockMs = 128;

class SimDevice {
 public:
  explicit SimDevice(const BootSpec& boot, cpa::Transport* link = nullptr);
  SimDevice(const SimDevice&) = delete;
  SimDevice& operator=(const SimDevice&) = delete;

  void loop(cpa::KeySet keys);  // one 10 ms pass of the main loop
  void host(const std::string& line);
  void set_usb(bool on) { tx_.is_plugged = on; }
  void set_bat(uint16_t mv) { plat_.next.bat_mv = mv; }
  void mic_block(uint32_t level);  // 128 ms of a tone at this RMS, while recording
  void mic_pcm(const std::string& pcm);  // these 16 kHz mono s16le bytes, while recording
  void set_tx_room(size_t bytes) { plat_.room = bytes; }  // free bytes in the USB TX ring
  bool reboot_due() const { return reboot_at_ != 0 && now_ >= reboot_at_; }
  void reboot();  // what the device does at reboot_due()
  uint32_t boots() const { return boot_; }

  std::string last_sent_id(const std::string& type);  // "" when the device sent none
  // docs/28 S1a, `pull`: the file.del lines for every item of the device's last file.items
  // (as cpad sends them once it has stored each segment).
  std::vector<std::string> dels_for_last_items();
  // No host line unread, the screen up to date and no G0 tap waiting for a second (gh #51);
  // still: and no slide on its way (S3).
  bool idle(bool still = true) const;
  bool drawn() const { return !rt_->shell().dirty(); }  // the screen shows the current state
  bool recording() const { return port_->engine && port_->engine->recording(); }
  uint32_t now() const { return now_; }
  const Frame& screen() const { return disp_.panel(); }
  cpa::Backlight backlight() const;
  uint32_t missing_glyphs() const { return disp_.missing_glyphs(); }  // □ in the frame on the panel
  uint32_t aa_misses() const { return disp_.aa_misses(); }  // rounded shapes without their own edge colour
  // docs/21 §9.2: the screen drawn whole once more, off the panel: how many texts ran into
  // an edge, and the last ("x,y text"). A shot's own frame may be a slide's last partial push.
  struct Edges {
    uint32_t n = 0;
    std::string last;
  };
  Edges edges() const;
  // v0.5 S3: the panel holds all that was drawn (every animation tick pushed what it drew),
  // the pushes since the last call, and the big ●'s breath step.
  bool panel_is_sprite() const { return disp_.panel_is_sprite(); }
  Pushes take_pushes() { return disp_.take_pushes(); }
  const Pushes& pushed() const { return disp_.total(); }  // since boot
  uint8_t breath() const { return disp_.breath(); }
  const char* top_id() const { return rt_->shell().top_id(); }

 private:
  void start();
  void watch_reboot();

  BootSpec spec_;
  uitest::FakeTransport tx_;
  cpa::Transport& link_;
  testsupport::FakeStore store_;
  testsupport::FakeStore cpafs_;  // the internal flash, the migration's source on store=sd
  cpa::NullStore null_;
  uitest::FakePlatform plat_;
  SimDisplay disp_;
  std::unique_ptr<SimRecPort> port_;
  std::unique_ptr<cpa::Runtime> rt_;
  uint32_t boot_ = 1;  // the NVS boot counter
  int reboots_seen_ = 0;
  uint32_t reboot_at_ = 0;
  uint32_t now_ = 0;
  uint32_t samples_ = 0;  // tone phase
};

}  // namespace cpasim
