#include "messages.h"

#include <cstring>

namespace cpa {
namespace proto {
namespace {

constexpr const char* kTypeNames[kMsgTypeCount] = {
    "hello",
    "ping",
    "pong",
    "ack",
    "error",
    "time.set",
    "projects.snapshot",
    "sessions.snapshot",
    "queue.snapshot",
    "outbox.item",
    "outbox.ack",
    "agent.spawn",
    "agent.reply",
    "agent.result",
    "queue.done",
    "inbox.dismiss",
    "file.list",
    "file.items",
    "file.get",
    "file.data",
    "file.del",
    "rec.list",
    "rec.items",
    "rec.act",
    "dests.snapshot",
#if CPA_TEST_HOOKS
    "debug.key",
    "debug.state",
    "debug.reboot",
    "debug.rec",
    "debug.mic",
    "debug.codec",
    "debug.lang",
#endif
};

}  // namespace

Message make_message(const MsgId& id, const Opt<MsgId>& re, const Body& body) {
  Message m;
  m.id = id;
  m.re = re;
  m.body = body;
  return m;
}

const char* type_name(MsgType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < kMsgTypeCount ? kTypeNames[i] : "";
}

Opt<MsgType> type_parse(const char* s, size_t n) {
  for (uint8_t i = 0; i < kMsgTypeCount; ++i) {
    if (std::strlen(kTypeNames[i]) == n && std::memcmp(kTypeNames[i], s, n) == 0) {
      return some(static_cast<MsgType>(i));
    }
  }
  return {};
}

}  // namespace proto
}  // namespace cpa
