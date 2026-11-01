// Shared helpers of the v0.3 recording device suites (docs/11 §11): the card under a test
// root (/cpatest, removed before and after), event waits on the audio task's queue, and
// the SHA-256 of a file on the card. Needs the FAT32 card inserted.
#pragma once

#include <Arduino.h>
#include <dirent.h>
#include <mbedtls/sha256.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "rec_port.h"

namespace rectest {

constexpr char kRoot[] = "/cpatest";
constexpr char kVfsRoot[] = "/sd/cpatest";
constexpr uint32_t kTestEpoch = 0x7E571235u;

// rm -r below a VFS path, bounded depth; the entries it could not delete.
inline int remove_tree(const std::string& path, int depth = 0) {
  DIR* d = ::opendir(path.c_str());
  if (!d) return 0;
  int failed = 0;
  while (const dirent* e = ::readdir(d)) {
    if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
    const std::string child = path + "/" + e->d_name;
    if (e->d_type == DT_DIR && depth < 4) failed += remove_tree(child, depth + 1);
    failed += (e->d_type == DT_DIR ? ::rmdir(child.c_str()) : ::unlink(child.c_str())) == 0 ? 0 : 1;
  }
  ::closedir(d);
  return failed;
}

// The next event of `kind` within ms (others are passed to `seen` and dropped).
template <class Seen>
inline bool wait_for(cpa::RecPort& port, cpa::rec::EvtKind kind, uint32_t ms, cpa::rec::RecEvt& out, Seen seen) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    (void)static_cast<bool>(Serial);  // TX kick (03 §3)
    cpa::rec::RecEvt e;
    if (!port.poll(e)) {
      delay(5);
      continue;
    }
    seen(e);
    if (e.kind == kind) {
      out = e;
      return true;
    }
  }
  return false;
}

// Hex SHA-256 of bytes [from, end) of a file on the card.
inline std::string sha256_of(const std::string& vfs_path, long from) {
  FILE* f = std::fopen(vfs_path.c_str(), "rb");
  if (!f) return std::string();
  std::fseek(f, from, SEEK_SET);
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  static uint8_t buf[2048];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) mbedtls_sha256_update_ret(&ctx, buf, n);
  std::fclose(f);
  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  char hex[65];
  for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  return std::string(hex, 64);
}

}  // namespace rectest
