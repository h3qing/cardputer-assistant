// The SD card latched while running (docs/20 U16, A9): a pulled card fails every save
// until a reboot. The screen says so ("SD 卡出错：插好后需重启", not E_STORAGE); with
// the internal cpafs attached, the capture is kept there instead (the next boot's
// migration moves it to the card); recording names the reboot too.
#include <unity.h>

#include <string>

#include "codec.h"
#include "core/support/fake_fat_port.h"
#include "core/support/fake_store.h"
#include "outbox_io.h"
#include "rec_overlay.h"
#include "runtime.h"
#include "sd_store.h"
#include "support/fake_io.h"
#include "support/fake_port.h"
#include "support/fake_rec_port.h"
#include "support/fixtures.h"
#include "support/runtime_rig.h"

using namespace cpa;
using namespace uitest;
using testsupport::FakeFatPort;
using testsupport::FakeStore;
using testsupport::FatCall;

void setUp() {}
void tearDown() {}

class Spare : public SpareStore {
 public:
  FakeStore fs;
  int mounts = 0;
  FileStore* mount() override {
    ++mounts;
    return &fs;
  }
};

struct CardRig {
  FakeFatPort port;
  SdStore sd{port};
  FakeTransport tx;
  FakePlatform plat;
  FakeDisplay disp;
  ProbeModule board{"board"};
  cpa::Module* mods[1] = {&board};
  Runtime rt{tx, sd, plat, disp, mods, 1};
  FakeRecPort rec;
  uint32_t now = 0;

  explicit CardRig(SpareStore* spare = nullptr) {
    TEST_ASSERT_TRUE(sd.prepare());
    if (spare) rt.attach_spare(*spare);
    RecBoot rb;
    rb.buffers = true;
    rt.attach_rec(rec, rb);
    StorageBoot sb;
    sb.plan.primary = Store::Sd;
    BootInfo b;
    b.ticket = test_ticket();
    b.dev = FixedStr<12>::from("020000000001");
    b.rst = FixedStr<16>::from("poweron");
    rt.begin(b, now, sb);
    rec.boot(rt.ids());
  }
  void pull_card() { port.fail_all(FatCall::OpenWrite, FatErr::Disk); }
  std::string typed_note(const char* text) {
    rt.shell().on_key(g0tap(), now += 10);
    for (const char* p = text; *p; ++p) rt.shell().on_key(chr(*p), now += 10);
    rt.shell().on_key(enter(), now += 10);
    FakeCanvas c;
    rt.shell().render(c, now += 10);
    return c.dump();
  }
};

static void test_a_card_pulled_while_running_names_the_reboot() {
  CardRig r;
  NoteBody n;
  n.text = UserText::from("first");
  TEST_ASSERT_TRUE(r.rt.save_note(n) == SaveResult::Saved);
  r.pull_card();
  const std::string screen = r.typed_note("lost");
  TEST_ASSERT_TRUE(r.sd.failed());
  TEST_ASSERT_TRUE_MESSAGE(screen.find("SD 卡出错：插好后需重启") != std::string::npos, screen.c_str());
  TEST_ASSERT_TRUE(screen.find("E_STORAGE") == std::string::npos);
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::Compose);  // the text is kept
  TEST_ASSERT_TRUE(r.rt.save_note(n) == SaveResult::CardLost);  // latched: every save says so
}

static void test_the_capture_is_kept_in_cpafs() {
  Spare spare;
  CardRig r(&spare);
  r.pull_card();
  const std::string screen = r.typed_note("keep me");
  TEST_ASSERT_EQUAL_INT(1, spare.mounts);
  TEST_ASSERT_TRUE_MESSAGE(screen.find("已暂存") != std::string::npos, screen.c_str());
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::None);  // kept: the editor closes
  proto::Codec codec;
  const ScanResult& scan = outbox_boot_scan(spare.fs, codec);
  TEST_ASSERT_EQUAL_UINT16(1, scan.core.n);  // a whole outbox item: the next boot migrates it
  char line[1024];
  TEST_ASSERT_TRUE(outbox_read(spare.fs, scan.core.ids[0], line, sizeof line) > 0);
  TEST_ASSERT_TRUE(std::string(line).find("keep me") != std::string::npos);
}

static void test_recording_names_the_reboot() {
  TEST_ASSERT_TRUE(std::string(rec_refusal_text(rec::RecRefusal::NoSd)).find("插卡后重启") != std::string::npos);
  CardRig r;
  r.now += 10;
  r.rt.loop(r.now, KeySet{});
  TEST_ASSERT_TRUE(r.rt.state().rec.avail);
  r.pull_card();
  NoteBody n;
  n.text = UserText::from("x");
  (void)r.rt.save_note(n);  // the latch
  r.now += 10;
  r.rt.loop(r.now, KeySet{});
  TEST_ASSERT_FALSE(r.rt.state().rec.avail);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_card_pulled_while_running_names_the_reboot);
  RUN_TEST(test_the_capture_is_kept_in_cpafs);
  RUN_TEST(test_recording_names_the_reboot);
  return UNITY_END();
}
