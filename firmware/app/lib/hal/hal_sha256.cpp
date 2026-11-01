#include "hal_sha256.h"

#include <esp_rom_crc.h>

namespace cpa {

void HalSha256::begin() {
  mbedtls_sha256_starts_ret(&ctx_, 0);  // 0: SHA-256, not SHA-224
}

void HalSha256::update(const uint8_t* data, size_t n) { mbedtls_sha256_update_ret(&ctx_, data, n); }

Sha256Hex HalSha256::finish() {
  static const char kHex[] = "0123456789abcdef";
  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&ctx_, digest);
  char hex[64];
  for (int i = 0; i < 32; ++i) {
    hex[2 * i] = kHex[digest[i] >> 4];
    hex[2 * i + 1] = kHex[digest[i] & 15];
  }
  return Sha256Hex::from(hex, 64);
}

uint32_t hal_crc32(const uint8_t* data, size_t n) { return esp_rom_crc32_le(0, data, static_cast<uint32_t>(n)); }

}  // namespace cpa
