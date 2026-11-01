// Script files: lines, comments, includes and error locations. The steps themselves are
// parsed in script_steps.cpp.
#include "script.h"

#include "script_words.h"

namespace cpasim {
namespace {

constexpr int kMaxDepth = 8;

struct Parser {
  const Reader& read;
  Script out;
};

bool fail(Parser& p, const std::string& where, const std::string& msg) {
  p.out.error = where + ": " + msg;
  return false;
}

// The line without its comment: '#' at the start or after a blank, outside "strings".
std::string strip_comment(const std::string& line) {
  bool quoted = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"' && (i == 0 || line[i - 1] != '\\')) quoted = !quoted;
    const bool after_blank = i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t';
    if (c == '#' && !quoted && after_blank) return line.substr(0, i);
  }
  return line;
}

std::string dir_of(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string resolve(const std::string& from, const std::string& name) {
  return !name.empty() && name[0] == '/' ? name : dir_of(from) + name;
}

bool parse_file(Parser& p, const std::string& path, const std::string& text, int depth);

bool include(Parser& p, const std::string& from, const std::string& where, const std::string& name, int depth) {
  if (name.empty()) return fail(p, where, "'include' needs a file");
  if (depth >= kMaxDepth) return fail(p, where, "includes nested too deep");
  const std::string path = resolve(from, name);
  std::string text;
  if (!p.read(path, text)) return fail(p, where, "cannot read '" + name + "'");
  return parse_file(p, path, text, depth + 1);
}

bool parse_line(Parser& p, const std::string& path, const std::string& where, const std::string& line, int depth) {
  std::string verb;
  std::string rest;
  words::head_tail(line, verb, rest);
  if (verb == "include") return include(p, path, where, rest, depth);
  Step st;
  StepParser parse = step_parser(verb, st.op);
  if (!parse) return fail(p, where, "unknown step '" + verb + "'");
  st.where = where;
  std::string err;
  if (!parse(rest, st, err)) return fail(p, where, err);
  p.out.steps.push_back(st);
  return true;
}

bool parse_file(Parser& p, const std::string& path, const std::string& text, int depth) {
  size_t at = 0;
  for (int lineno = 1; at <= text.size(); ++lineno) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string line = words::trim(strip_comment(text.substr(at, end - at)));
    at = end + 1;
    if (line.empty()) continue;
    if (!parse_line(p, path, path + ":" + std::to_string(lineno), line, depth)) return false;
  }
  return true;
}

}  // namespace

Script parse_script(const std::string& path, const Reader& read) {
  Parser p{read, Script{}};
  std::string text;
  if (!read(path, text)) {
    p.out.error = "cannot read '" + path + "'";
    return p.out;
  }
  if (!parse_file(p, path, text, 0)) p.out.steps.clear();
  return p.out;
}

}  // namespace cpasim
