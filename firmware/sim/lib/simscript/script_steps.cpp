// The step verbs of a sim script (script.h): each turns the rest of its line into a Step.
#include <cstring>

#include "chord_detector.h"
#include "key_names.h"
#include "script_words.h"
#include "text_fit.h"

namespace cpasim {
namespace words {

std::string trim(const std::string& s) {
  const char* blank = " \t\r\n";
  const size_t a = s.find_first_not_of(blank);
  if (a == std::string::npos) return std::string();
  return s.substr(a, s.find_last_not_of(blank) - a + 1);
}

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::string head;
  std::string tail = trim(s);
  while (!tail.empty()) {
    head_tail(tail, head, tail);
    out.push_back(head);
  }
  return out;
}

void head_tail(const std::string& s, std::string& head, std::string& tail) {
  const std::string t = trim(s);
  const size_t sp = t.find_first_of(" \t");
  head = t.substr(0, sp);
  tail = sp == std::string::npos ? std::string() : trim(t.substr(sp));
}

bool to_u32(const std::string& s, uint32_t& out) {
  if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos) return false;
  out = static_cast<uint32_t>(std::stoul(s));
  return true;
}

}  // namespace words

namespace {

struct Alias {
  const char* name;
  const char* combo;
};

// The arrow legends of the ; . , / keys (docs/13: "Tab -> ; . -> Enter"), and Esc.
constexpr Alias kAliases[] = {{"up", ";"}, {"down", "."}, {"left", ","}, {"right", "/"}, {"esc", "fn+`"}};

std::string expand(const std::string& token) {
  for (const Alias& a : kAliases) {
    if (token == a.name) return a.combo;
  }
  return token;
}

bool combos(const std::vector<std::string>& ts, std::vector<cpa::KeySet>& out, std::string& err) {
  for (const std::string& t : ts) {
    cpa::KeySet k;
    if (!parse_combo(t, k)) {
      err = "unknown key '" + t + "'";
      return false;
    }
    out.push_back(k);
  }
  return true;
}

bool fields(const std::string& rest, std::string& out, std::string& err) {
  const std::string t = words::trim(rest);
  if (t.empty()) return true;
  if (t.size() < 2 || t.front() != '{' || t.back() != '}') {
    err = "fields must be one JSON object {...}";
    return false;
  }
  out = words::trim(t.substr(1, t.size() - 2));
  return true;
}

bool boot_option(const std::string& opt, BootSpec& b) {
  uint32_t mv = 0;
  if (opt == "store=sd" || opt == "store=internal" || opt == "store=none") {
    b.store = opt == "store=sd" ? cpa::Store::Sd : (opt == "store=internal" ? cpa::Store::Internal : cpa::Store::None);
    return true;
  }
  if (opt == "usb=on" || opt == "usb=off") {
    b.usb = opt == "usb=on";
    return true;
  }
  if (opt == "lang=zh" || opt == "lang=en") {
    b.lang = opt == "lang=en" ? cpa::Lang::En : cpa::Lang::Zh;
    return true;
  }
  if (opt.rfind("bat=", 0) != 0 || !words::to_u32(opt.substr(4), mv) || mv > 0xFFFF) return false;
  b.bat_mv = static_cast<uint16_t>(mv);
  return true;
}

bool parse_boot(const std::string& rest, Step& st, std::string& err) {
  for (const std::string& opt : words::split(rest)) {
    if (!boot_option(opt, st.boot)) {
      err = "unknown boot option '" + opt + "'";
      return false;
    }
  }
  return true;
}

bool parse_host(const std::string& rest, Step& st, std::string& err) {
  std::string tail;
  words::head_tail(rest, st.type, tail);
  if (st.type.empty()) {
    err = "'host' needs a message type";
    return false;
  }
  return fields(tail, st.text, err);
}

bool parse_reply(const std::string& rest, Step& st, std::string& err) {
  std::string tail;
  words::head_tail(rest, st.type, tail);
  words::head_tail(tail, st.re_type, tail);
  if (st.re_type.empty()) {
    err = "'reply' needs a type and the type it answers";
    return false;
  }
  return fields(tail, st.text, err);
}

bool parse_press(const std::string& rest, Step& st, std::string& err) {
  std::vector<std::string> ts = words::split(rest);
  st.ms = kPressMs;
  if (!ts.empty() && ts.back().rfind("ms=", 0) == 0) {
    if (!words::to_u32(ts.back().substr(3), st.ms)) {
      err = "bad hold '" + ts.back() + "'";
      return false;
    }
    ts.pop_back();
  }
  if (ts.empty()) {
    err = "'press' needs a key";
    return false;
  }
  return combos(ts, st.keys, err);
}

bool parse_hold(const std::string& rest, Step& st, std::string& err) {
  const std::vector<std::string> ts = words::split(rest);
  if (ts.size() != 2 || !words::to_u32(ts[1], st.ms)) {
    err = "'hold' takes a key and a number of ms";
    return false;
  }
  return combos({ts[0]}, st.keys, err);
}

// Unshifted key first, then Shift + the key whose shifted character it is.
bool key_of_char(char c, cpa::KeySet& out) {
  for (uint8_t k = 0; k < cpa::kMatrixKeys; ++k) {
    if (!cpa::is_modifier(k) && cpa::key_base(k) == c) {
      out = cpa::KeySet{}.with(k);
      return true;
    }
  }
  for (uint8_t k = 0; k < cpa::kMatrixKeys; ++k) {
    if (!cpa::is_modifier(k) && cpa::key_shifted(k) == c) {
      out = cpa::KeySet{}.with(cpa::key::kShift).with(k);
      return true;
    }
  }
  return false;
}

bool parse_type(const std::string& rest, Step& st, std::string& err) {
  st.ms = kPressMs;
  for (size_t i = 0; i < rest.size();) {
    const cpa::Utf8Char ch = cpa::utf8_at(rest.c_str(), rest.size(), i);
    cpa::KeySet k;
    if (ch.cp < 0x20 || ch.cp > 0x7E || !key_of_char(static_cast<char>(ch.cp), k)) {
      err = "'type' cannot type '" + rest.substr(i, ch.len) + "'";
      return false;
    }
    st.keys.push_back(k);
    i += ch.len;
  }
  if (!st.keys.empty()) return true;
  err = "'type' needs text";
  return false;
}

bool parse_keys(const std::string& rest, Step& st, std::string& err) {
  const std::vector<std::string> ts = words::split(rest);
  if (ts.empty() && st.op == Op::Down) {
    err = "'down' needs a key";
    return false;
  }
  return combos(ts, st.keys, err);
}

bool parse_number(const std::string& rest, const char* verb, uint32_t& out, std::string& err) {
  if (words::to_u32(words::trim(rest), out)) return true;
  err = std::string("'") + verb + "' needs a number of " + (std::strcmp(verb, "bat") == 0 ? "mV" : "ms");
  return false;
}

bool parse_wait(const std::string& rest, Step& st, std::string& err) { return parse_number(rest, "wait", st.ms, err); }

bool parse_bat(const std::string& rest, Step& st, std::string& err) { return parse_number(rest, "bat", st.value, err); }

bool parse_usb(const std::string& rest, Step& st, std::string& err) {
  const std::string t = words::trim(rest);
  if (t != "on" && t != "off") {
    err = "'usb' takes on or off";
    return false;
  }
  st.value = t == "on" ? 1 : 0;
  return true;
}

// "speech" or an RMS level.
bool parse_level(const std::string& s, Step& st) {
  st.speech = s == "speech";
  return st.speech || words::to_u32(s, st.value);
}

bool parse_mic(const std::string& rest, Step& st, std::string& err) {
  const std::vector<std::string> ts = words::split(rest);
  st.value = kMicLevel;
  const bool ok = (ts.size() == 1 || ts.size() == 2) && words::to_u32(ts[0], st.ms) &&
                  (ts.size() == 1 || parse_level(ts[1], st));
  if (!ok) err = "'mic' takes ms and an optional level or speech";
  return ok;
}

// frames <name> <n> <ms> [mic=level|mic=speech] [caption]
bool parse_frames(const std::string& rest, Step& st, std::string& err) {
  std::string n, ms, tail;
  words::head_tail(rest, st.type, tail);
  words::head_tail(tail, n, tail);
  words::head_tail(tail, ms, tail);
  st.value = kMicLevel;
  std::string mic, after;
  words::head_tail(tail, mic, after);
  if (mic.rfind("mic=", 0) == 0) {
    st.mic = parse_level(mic.substr(4), st);
    tail = after;
  }
  const bool ok = !st.type.empty() && words::to_u32(n, st.count) && st.count >= 1 && st.count <= 50 &&
                  words::to_u32(ms, st.ms) && st.ms >= 10 && (mic.rfind("mic=", 0) != 0 || st.mic);
  if (!ok) err = "'frames' takes a name, 1-50 shots, ms apart (>= 10) and an optional mic=level|speech";
  st.text = tail.empty() ? st.type : tail;
  return ok;
}

bool parse_shot(const std::string& rest, Step& st, std::string& err) {
  words::head_tail(rest, st.type, st.text);
  if (st.type.empty()) {
    err = "'shot' needs a name";
    return false;
  }
  if (st.text.empty()) st.text = st.type;
  return true;
}

// docs/28 S1a: `pull` takes no arguments (the simulated daemon takes what it is shown).
bool parse_pull(const std::string& rest, Step& st, std::string& err) {
  (void)st;
  if (words::trim(rest).empty()) return true;
  err = "'pull' takes nothing";
  return false;
}

struct Verb {
  const char* name;
  Op op;
  StepParser parse;
};

constexpr Verb kVerbs[] = {
    {"boot", Op::Boot, parse_boot},   {"host", Op::Host, parse_host}, {"reply", Op::Reply, parse_reply},
    {"press", Op::Press, parse_press}, {"hold", Op::Press, parse_hold}, {"type", Op::Press, parse_type},
    {"down", Op::Down, parse_keys},   {"up", Op::Up, parse_keys},     {"wait", Op::Wait, parse_wait},
    {"usb", Op::Usb, parse_usb},      {"bat", Op::Bat, parse_bat},    {"mic", Op::Mic, parse_mic},
    {"shot", Op::Shot, parse_shot},   {"frames", Op::Frames, parse_frames}, {"pull", Op::Pull, parse_pull},
};

}  // namespace

bool parse_combo(const std::string& token, cpa::KeySet& out) {
  const std::string t = expand(token);
  cpa::KeySet keys;
  size_t at = 0;
  while (at <= t.size()) {
    size_t plus = t.find('+', at);
    if (plus == std::string::npos) plus = t.size();
    const cpa::Opt<uint8_t> k = cpa::proto::key_index(t.c_str() + at, plus - at);
    if (!k.has) return false;
    keys = keys.with(k.value);
    at = plus + 1;
  }
  out = keys;
  return !keys.empty();
}

StepParser step_parser(const std::string& verb, Op& op) {
  for (const Verb& v : kVerbs) {
    if (verb == v.name) {
      op = v.op;
      return v.parse;
    }
  }
  return nullptr;
}

}  // namespace cpasim
