// cpa-sim (gh #32): the firmware's runtime on the host.
//   cpa-sim serve [options]   the device on a pty, for cpad and the HIL harness (serve.h)
//   cpa-sim shots <script.cpas> [--out DIR] [--scale N] [--columns N]
// plays the script (lib/simscript/script.h) and writes DIR/NNN-<name>.png for every
// `shot`, DIR/contact.png with all of them and DIR/shots.json (name, caption, time,
// backlight, module in front, □ in the frame, settled, and since the shot before: full pushes,
// partial pushes and their pixels, the SPI work of docs/19 §7.5). A shot is the frame the panel
// holds, also when the backlight is off or dimmed (the sheet's caption says so); one
// taken before the screen settled (500 ms) is still saved, with a warning on stderr.
// Exit status: 0 done, 1 the run failed, 2 usage or script error.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "contact.h"
#include "player.h"
#include "png.h"
#include "script.h"
#include "serve.h"
#include "sim_files.h"

namespace {

using namespace cpasim;

struct Args {
  std::string script;
  std::string out = "out";
  int scale = 2;
  int columns = 4;
  std::string lang;  // docs/21: "zh" or "en" overrides every boot step's lang= (one script, both languages)
};

int usage() {
  std::fprintf(stderr, "usage: cpa-sim shots <script.cpas> [--out DIR] [--scale N] [--columns N] [--lang zh|en]\n%s", kServeUsage);
  return 2;
}

bool parse_args(int argc, char** argv, Args& a) {
  if (argc < 3 || std::string(argv[1]) != "shots") return false;
  a.script = argv[2];
  for (int i = 3; i + 1 < argc; i += 2) {
    const std::string opt = argv[i];
    if (opt == "--out") a.out = argv[i + 1];
    else if (opt == "--scale") a.scale = std::atoi(argv[i + 1]);
    else if (opt == "--columns") a.columns = std::atoi(argv[i + 1]);
    else if (opt == "--lang") a.lang = argv[i + 1];
    else return false;
  }
  const bool lang_ok = a.lang.empty() || a.lang == "zh" || a.lang == "en";
  return (argc - 3) % 2 == 0 && a.scale >= 1 && a.scale <= 8 && a.columns >= 1 && lang_ok;
}

std::string json_str(const std::string& s) {
  std::string out = "\"";
  for (const char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
      out += buf;
    } else {
      out += c;
    }
  }
  return out + "\"";
}

std::string shot_json(const Shot& s, const std::string& file) {
  return "{\"name\":" + json_str(s.name) + ",\"file\":" + json_str(file) + ",\"caption\":" + json_str(s.caption) +
         ",\"t_ms\":" + std::to_string(s.at_ms) + ",\"backlight\":\"" + backlight_name(s.backlight) +
         "\",\"top\":" + json_str(s.top) + ",\"missing_glyphs\":" + std::to_string(s.missing_glyphs) +
         ",\"settled\":" + (s.settled ? "true" : "false") + ",\"pushes\":{\"full\":" + std::to_string(s.pushes.full) +
         ",\"partial\":" + std::to_string(s.pushes.partial) + ",\"partial_px\":" + std::to_string(s.pushes.partial_px) +
         "}}";
}

bool write_outputs(const Args& a, const std::vector<Shot>& shots) {
  std::error_code ec;
  std::filesystem::create_directories(a.out, ec);
  std::string json = "[\n";
  std::vector<Captioned> sheet;
  for (size_t i = 0; i < shots.size(); ++i) {
    char num[8];
    std::snprintf(num, sizeof num, "%03u", static_cast<unsigned>(i + 1));
    const std::string file = std::string(num) + "-" + safe_name(shots[i].name) + ".png";
    if (!write_png(a.out + "/" + file, shots[i].frame, a.scale)) return false;
    std::printf("%s/%s  %s\n", a.out.c_str(), file.c_str(), shots[i].caption.c_str());
    if (!shots[i].settled) std::fprintf(stderr, "cpa-sim: warning: %s was taken before the screen settled\n", file.c_str());
    json += "  " + shot_json(shots[i], file) + (i + 1 < shots.size() ? ",\n" : "\n");
    const bool lit = shots[i].backlight == cpa::Backlight::On;
    const std::string note = lit ? "" : std::string(" [backlight ") + backlight_name(shots[i].backlight) + "]";
    sheet.push_back(Captioned{&shots[i].frame, std::string(num) + " " + shots[i].caption + note});
  }
  if (!write_png(a.out + "/contact.png", contact_sheet(sheet, a.columns), a.scale)) return false;
  std::printf("%s/contact.png\n", a.out.c_str());
  return write_file(a.out + "/shots.json", json + "]\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "serve") return serve_main(argc - 2, argv + 2);
  Args a;
  if (!parse_args(argc, argv, a)) return usage();
  const Script script = parse_script(a.script, read_file);
  if (!script.error.empty()) {
    std::fprintf(stderr, "cpa-sim: %s\n", script.error.c_str());
    return 2;
  }
  std::vector<Step> steps = script.steps;
  for (Step& st : steps) {
    if (!a.lang.empty()) st.boot.lang = a.lang == "en" ? cpa::Lang::En : cpa::Lang::Zh;
  }
  const PlayResult run = play(steps);
  if (!run.error.empty()) {
    std::fprintf(stderr, "cpa-sim: %s\n", run.error.c_str());
    return 1;
  }
  if (!write_outputs(a, run.shots)) {
    std::fprintf(stderr, "cpa-sim: cannot write to %s\n", a.out.c_str());
    return 1;
  }
  return 0;
}
