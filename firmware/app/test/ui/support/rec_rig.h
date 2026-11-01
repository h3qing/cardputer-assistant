// RecRig: a Runtime with the audio task on the host (FakeRecPort: a real RecEngine on the
// fake card), booted with the SD card primary (or the internal store), the scan's
// IndexReady seen and the handshake done. Keys start and stop recordings.
#pragma once

#include <unity.h>

#include <string>

#include "rec_core.h"
#include "runtime.h"
#include "support/fake_rec_port.h"
#include "support/runtime_rig.h"

namespace uitest {

struct RecRig {
  RuntimeRig r{false};
  FakeRecPort port;

  explicit RecRig(bool sd = true, bool scan = true, uint64_t free_bytes = 64ull << 30) {
    cpa::RecBoot rb;
    rb.buffers = sd;
    rb.free_known = true;
    rb.free_bytes = free_bytes;
    r.rt.attach_rec(port, rb);
    cpa::BootInfo b;
    b.ticket = test_ticket();
    b.dev = cpa::FixedStr<12>::from("020000000001");
    b.rst = cpa::FixedStr<16>::from("poweron");
    cpa::StorageBoot sb;
    sb.plan = cpa::StoragePlan{sd ? cpa::Store::Sd : cpa::Store::Internal, false, cpa::StoreNote::None};
    r.rt.begin(b, r.now, sb);
    port.boot(r.rt.ids(), scan);
    r.step();  // the scan's IndexReady
    r.handshake();
  }

  void hold_g0(uint32_t ms) {
    const cpa::KeySet g0 = cpa::KeySet{}.with(cpa::key::kG0);
    for (uint32_t t = 0; t < ms; t += 10) r.step(10, g0);
  }
  void release() { r.step(10); }
  // G0 let go, and its grace runs out without a re-press (gh #51): the memo's Stop goes out.
  void let_go() {
    release();
    for (uint32_t t = 0; t < cpa::rec::kMemoGraceMs; t += 10) r.step(10);
  }
  void steps(int n = 3) {
    for (int i = 0; i < n; ++i) r.step(10);
  }

  // A memo of ms milliseconds: G0 held 500 ms, the start, audio, let go.
  void memo(uint32_t ms) {
    hold_g0(600);
    port.feed(ms);
    let_go();
    steps();
  }

  // The host id the next send() uses.
  std::string next_host_id() const { return "h" + std::to_string(r.host_id); }

  cpa::proto::Decoded last(const char* t) {
    const auto v = r.tx.of_type(t);
    TEST_ASSERT_FALSE_MESSAGE(v.empty(), t);
    return v.back();
  }
  cpa::proto::ErrorMsg last_error() { return std::get<cpa::proto::ErrorMsg>(last("error").msg.body); }
};

}  // namespace uitest
