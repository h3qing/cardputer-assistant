// Sha256Port on mbedtls (the ESP32-S3 SHA peripheral): the SHA-256 of each segment's
// PCM data, updated block by block while recording (docs/11 §4.4). Device only.
#pragma once

#include <mbedtls/sha256.h>

#include "rec_file_port.h"

namespace cpa {

class HalSha256 : public rec::Sha256Port {
 public:
  HalSha256() { mbedtls_sha256_init(&ctx_); }
  ~HalSha256() override { mbedtls_sha256_free(&ctx_); }
  void begin() override;
  void update(const uint8_t* data, size_t n) override;
  Sha256Hex finish() override;

 private:
  mbedtls_sha256_context ctx_;
};

uint32_t hal_crc32(const uint8_t* data, size_t n);  // esp_rom_crc32_le, zlib-compatible

}  // namespace cpa
