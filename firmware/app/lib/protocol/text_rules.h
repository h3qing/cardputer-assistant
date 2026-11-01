// String rules of protocol v0 (common.schema.json, limits.json), written by hand so
// the firmware needs no regex engine. Lengths are UTF-8 bytes unless noted.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cpa {
namespace proto {

bool utf8_valid(const char* s, size_t n);  // strict: no overlongs, surrogates or > U+10FFFF
bool no_controls(const char* s, size_t n);  // no U+0000..U+001F and no U+007F
// Every byte within [lo, hi] and min <= n <= max.
bool ascii_span(const char* s, size_t n, char lo, char hi, size_t min, size_t max);

bool is_id(const char* s, size_t n);           // ^[A-Za-z0-9-]{1,24}$
bool is_type(const char* s, size_t n);         // ^[a-z][a-z0-9_.]{1,31}$
bool is_monoid_text(const char* s, size_t n);  // monoid_pattern (format only)
bool is_boot_text(const char* s, size_t n);    // boot_pattern
bool is_project_key(const char* s, size_t n);  // ^[a-z0-9][a-z0-9-]{0,11}$
bool is_sid(const char* s, size_t n);          // ^[0-9a-f]{8}$
bool is_qid(const char* s, size_t n);          // ^q[0-9]{1,10}$
bool is_dev(const char* s, size_t n);          // ^[0-9a-f]{12}$
bool is_fw(const char* s, size_t n);           // ^cpa-fw/[!-~]{1,24}$
bool is_rst(const char* s, size_t n);          // ^[a-z0-9_]{1,16}$
bool is_module_id(const char* s, size_t n);    // ^[a-z][a-z0-9_]{1,15}$
bool is_app(const char* s, size_t n);          // ^[!-~]{1,32}$
bool is_short(const char* s, size_t n);        // ^[!-~]{1,16}$
bool is_field_path(const char* s, size_t n);   // error.field ^[!-~]{1,48}$
bool is_err_msg(const char* s, size_t n);      // error.msg ^[ -~]{1,80}$
bool is_sha256(const char* s, size_t n);       // v0.3 sha256_pattern ^[0-9a-f]{64}$
bool is_rec_cursor(const char* s, size_t n);   // v0.4.1 rec_cursor_pattern ^[A-Za-z0-9_-]{1,32}$
bool is_host_id(const char* s, size_t n);      // v0.4.1 host_id_pattern ^[a-z0-9-]{8,32}$
// v0.4.1 dest_pattern ^[a-z][a-z0-9-]{0,11}(\.[a-z0-9-]{1,12}){0,2}$, at most dest_max bytes.
bool is_dest(const char* s, size_t n);
// v0.5 dests.snapshot rows (docs/14 §7.2). The row id is ^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,11})?$:
// names of at most 12 keep every row id inside dest_pattern (docs/25 §2.2).
bool is_dest_row_id(const char* s, size_t n);
bool is_dest_kind(const char* s, size_t n);   // ^[a-z]{3,7}$
bool is_dest_key(const char* s, size_t n);    // ^[a-z]$
bool is_dest_flags(const char* s, size_t n);  // ^[a-z]{0,8}$ (docs/25 §2.2 fl)
bool is_dest_label(const char* s, size_t n);  // 1..24 B, no controls, '"' or '\'

bool is_project_name(const char* s, size_t n);  // 1..24 B, no controls
bool is_session_name(const char* s, size_t n);  // 1..32 B, no controls
bool is_queue_text(const char* s, size_t n);    // 1..80 B, no controls
bool is_user_text(const char* s, size_t n);     // 1..500 B, no controls (one line)
bool is_host_label(const char* s, size_t n);    // v0.4.1: 1..24 B, no controls
bool is_rec_title(const char* s, size_t n);     // v0.4.1: 1..48 B, no controls
// userText that, after leading whitespace (Python \s), does not start with '-'.
bool is_agent_text(const char* s, size_t n);

}  // namespace proto
}  // namespace cpa
