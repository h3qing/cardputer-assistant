// Error codes of protocol v0 (05 §6.8). The wire names live in protocol/errors.h;
// the enum is here because core slices (agent requests, outbox acks) carry codes.
#pragma once

#include <cstdint>

namespace cpa {

enum class ErrCode : uint8_t {
  None = 0,
  Parse,
  TooLarge,
  Version,
  UnknownType,
  BadField,
  NotReady,
  Storage,
  UnknownProject,
  UnknownSession,
  UnknownItem,
  NoAgent,
  Spawn,
  NotReplyable,
  ReplyForked,
  Busy,
  Internal,
};

constexpr uint8_t kErrCodeCount = static_cast<uint8_t>(ErrCode::Internal) + 1;

}  // namespace cpa
