// SHA-256 (FIPS 180-4) for the simulator's audio task (gh #32). The device hashes each
// segment's PCM with mbedtls (hal/hal_sha256); the native tests' FakeSha256 is not SHA-256,
// and cpad checks every pulled segment against the manifest's digest, so recordings made on
// `cpa-sim serve` need the real one. Standard library only.
#pragma once

#include <cstddef>
#include <cstdint>

#include "rec_file_port.h"

namespace cpasim {

class Sha256 : public cpa::rec::Sha256Port {
 public:
  void begin() override;
  void update(const uint8_t* data, size_t n) override;
  cpa::Sha256Hex finish() override;  // 64 lower-case hex digits

 private:
  void block(const uint8_t* p);  // one 64-byte block into h_

  uint32_t h_[8] = {};
  uint8_t buf_[64] = {};
  size_t used_ = 0;     // bytes waiting in buf_
  uint64_t bytes_ = 0;  // message length so far
};

}  // namespace cpasim
