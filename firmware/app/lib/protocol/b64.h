// Standard base64 with padding (RFC 4648 §4) for file.data (docs/11 §5.3). Table-driven:
// the constant-time mbedtls encoder cost ~1.6 ms per 1380 B on the device
// (docs/gates/m3-device-gates.md §3.2).
#pragma once

#include <cstddef>
#include <cstdint>

namespace cpa {
namespace proto {

// Writes 4 * ceil(n / 3) characters (no NUL) and returns that count.
size_t b64_encode(const uint8_t* in, size_t n, char* out);
// Decodes canonical-length input (a multiple of 4, '=' only as the last one or two
// characters) into out; the byte count, or -1 when invalid or longer than cap.
long b64_decode(const char* in, size_t n, uint8_t* out, size_t cap);
// The file.data b64 schema: 4..1840 characters of padded base64.
bool is_b64(const char* s, size_t n);

}  // namespace proto
}  // namespace cpa
