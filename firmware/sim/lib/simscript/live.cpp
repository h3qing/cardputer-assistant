#include "live.h"

#include "presses.h"
#include "script_words.h"

namespace cpasim {
namespace {

const char* const kStdin = "stdin";

LiveCmd error(const std::string& msg) {
  LiveCmd c;
  c.kind = LiveKind::Error;
  c.error = msg;
  return c;
}

bool script_only(Op op) {
  return op == Op::Boot || op == Op::Host || op == Op::Reply || op == Op::Mic || op == Op::Pull;
}

// "stdin:1: unknown key 'x'" -> "unknown key 'x'"
std::string without_where(const std::string& err) {
  const std::string prefix = std::string(kStdin) + ":1: ";
  return err.compare(0, prefix.size(), prefix) == 0 ? err.substr(prefix.size()) : err;
}

}  // namespace

LiveCmd parse_live(const std::string& line) {
  std::string verb;
  std::string rest;
  words::head_tail(words::trim(line), verb, rest);
  if (verb == "quit") {
    LiveCmd c;
    c.kind = LiveKind::Quit;
    return c;
  }
  if (verb == "include") return error("'include' only works in a script");
  const Script s = parse_script(kStdin, [&line](const std::string& path, std::string& out) {
    out = line;
    return path == kStdin;
  });
  if (!s.error.empty()) return error(without_where(s.error));
  if (s.steps.empty()) return LiveCmd{};
  if (script_only(s.steps.front().op)) return error("'" + verb + "' only works in a script: on serve the host is cpad");
  LiveCmd c;
  c.kind = LiveKind::Step;
  c.step = s.steps.front();
  return c;
}

bool is_key_step(Op op) { return op == Op::Press || op == Op::Down || op == Op::Up || op == Op::Wait; }

void KeyFeed::add(const Step& st) {
  switch (st.op) {
    case Op::Press:
      for (const cpa::KeySet& k : press_samples(st.keys, st.ms, held_)) q_.push_back(k);
      return;
    case Op::Down: held_ = with_keys(held_, st.keys); q_.push_back(held_); return;
    case Op::Up: held_ = without_keys(held_, st.keys); q_.push_back(held_); return;
    case Op::Wait: q_.insert(q_.end(), (st.ms + kTickMs - 1) / kTickMs, held_); return;
    default: return;
  }
}

cpa::KeySet KeyFeed::next() {
  if (q_.empty()) return held_;
  const cpa::KeySet k = q_.front();
  q_.pop_front();
  return k;
}

}  // namespace cpasim
