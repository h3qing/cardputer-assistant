// Codec for protocol v0 lines (05 §5.8, §6; decode order in protocol/README.md and
// tools/wire.py). One Codec owns the single reusable JsonDocument (05 §1.2) and the
// last decode result, so it is the only stateful object here. The result is written
// in place: a by-value Message (~1.5 KB) copied through the decoders overflowed the
// 16 KB loop stack (05 §5.13; checked by test/core/support/stack_gate.py).
#pragma once

#include <ArduinoJson.h>

#include <cstddef>

#include "err_code.h"
#include "messages.h"
#include "result.h"

namespace cpa {
namespace proto {

using Line = FixedStr<2048>;  // one encoded line including the trailing '\n'

enum class Outcome : uint8_t {
  Ok,
  Noise,    // does not start with '{': dropped and counted, never answered
  Ignored,  // unknown type on a reply: dropped
  Error,    // see code (and field for E_BAD_FIELD)
};

struct Decoded {
  Outcome outcome = Outcome::Error;
  ErrCode code = ErrCode::None;
  FieldPath field;
  Opt<MsgId> reply_to;  // the id the receiver must answer exactly once, if any
  Message msg;          // valid when outcome == Ok
};

class Codec {
 public:
  // One line; a trailing '\n' is optional and every '\r' is ignored. The result is
  // owned by the codec and valid until the next decode(); encode() leaves it alone.
  const Decoded& decode(const char* raw, size_t n);
  // Serialised line with '\n'; ErrCode::TooLarge when it would exceed line_max.
  Result<Line, ErrCode> encode(const Message& m);

 private:
  JsonDocument doc_;
  Decoded last_;
};

}  // namespace proto
}  // namespace cpa
