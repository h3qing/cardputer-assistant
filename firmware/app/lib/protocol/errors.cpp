#include "errors.h"

#include <cstring>

namespace cpa {
namespace {

constexpr const char* kNames[kErrCodeCount] = {
    "",
    "E_PARSE",
    "E_TOO_LARGE",
    "E_VERSION",
    "E_UNKNOWN_TYPE",
    "E_BAD_FIELD",
    "E_NOT_READY",
    "E_STORAGE",
    "E_UNKNOWN_PROJECT",
    "E_UNKNOWN_SESSION",
    "E_UNKNOWN_ITEM",
    "E_NO_AGENT",
    "E_SPAWN",
    "E_NOT_REPLYABLE",
    "E_REPLY_FORKED",
    "E_BUSY",
    "E_INTERNAL",
};

}  // namespace

const char* err_name(ErrCode code) {
  const auto i = static_cast<uint8_t>(code);
  return i < kErrCodeCount ? kNames[i] : "";
}

Opt<ErrCode> err_parse(const char* s, size_t n) {
  for (uint8_t i = 1; i < kErrCodeCount; ++i) {
    if (std::strlen(kNames[i]) == n && std::memcmp(kNames[i], s, n) == 0) return some(static_cast<ErrCode>(i));
  }
  return {};
}

}  // namespace cpa
