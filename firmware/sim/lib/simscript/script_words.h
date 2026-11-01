// Small text helpers of the script parser (script.cpp, script_steps.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "script.h"

namespace cpasim {
namespace words {

std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s);                   // on blanks
void head_tail(const std::string& s, std::string& head, std::string& tail);  // first word, the rest
bool to_u32(const std::string& s, uint32_t& out);

}  // namespace words

// One step's arguments -> st; false with err set when they are wrong.
using StepParser = bool (*)(const std::string& rest, Step& st, std::string& err);

// The parser of a step verb ("press", "host", ...), nullptr when unknown; op set.
StepParser step_parser(const std::string& verb, Op& op);

}  // namespace cpasim
