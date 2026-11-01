// SimRecPort (gh #32): the simulator's audio task. It is the native tests' FakeRecPort (a
// real RecEngine on an in-memory card, run synchronously), with two things the device has
// and the fake does not: real SHA-256 of each segment's PCM (cpad checks every pulled
// segment against it) and the firmware's own version in the manifests.
#pragma once

#include "mono_id.h"
#include "rec_engine.h"
#include "runtime.h"
#include "sha256.h"
#include "support/fake_rec_port.h"

namespace cpasim {

class SimRecPort : public uitest::FakeRecPort {
 public:
  // FakeRecPort::boot() with the real hash: the audio task starts after the runtime, with
  // its id counter, and scans the card first.
  void boot(cpa::MonoCounter& ids) {
    cpa::rec::EngineDeps d;
    d.fs = &fs;
    d.sha = &sha256_;
    d.ids = &ids;
    d.index = &idx;
    d.xfer = &buf;
    d.fw = cpa::FixedStr<31>::from(cpa::kFwVersion);
    engine.reset(new cpa::rec::RecEngine(d));
    finish_scan();
  }

 private:
  Sha256 sha256_;
};

}  // namespace cpasim
