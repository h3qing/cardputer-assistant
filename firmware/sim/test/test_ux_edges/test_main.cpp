// docs/21 §9.2: every ux script and both tours, in both languages, keep their text off the
// screen's edges: no shot whose frame drew text past the left or right edge, or whose ink
// lit the last column (FbCanvas::text). The fuzz test checks the same on random presses;
// these are the screens the scripts reach and the fuzz test may not.
#include <unity.h>

#include <filesystem>
#include <string>
#include <vector>

#include "lang.h"
#include "player.h"
#include "script.h"
#include "sim_files.h"

using namespace cpasim;

void setUp() { cpa::set_lang(cpa::Lang::Zh); }
void tearDown() { cpa::set_lang(cpa::Lang::Zh); }

namespace {

std::vector<std::string> scripts() {  // pio test runs in firmware/sim
  std::vector<std::string> out{"scripts/v03_tour.cpas", "scripts/v04_tour.cpas"};
  for (const auto& e : std::filesystem::directory_iterator("scripts/ux")) {
    if (e.path().extension() == ".cpas") out.push_back(e.path().string());
  }
  return out;
}

std::string edges(const std::string& path, cpa::Lang lang) {
  const Script s = parse_script(path, read_file);
  if (!s.error.empty()) return s.error + "\n";
  std::vector<Step> steps = s.steps;
  for (Step& st : steps) st.boot.lang = lang;
  const PlayResult r = play(steps);
  std::string bad = r.error.empty() ? "" : r.error + "\n";
  for (const Shot& shot : r.shots) {
    if (shot.offscreen == 0) continue;
    bad += path + " " + cpa::lang_code(lang) + " " + shot.name + ": " + shot.offscreen_text + "\n";
  }
  return bad;
}

}  // namespace

static void test_no_script_draws_text_into_an_edge() {
  const std::vector<std::string> all = scripts();
  TEST_ASSERT_TRUE(all.size() > 30);
  std::string bad;
  for (const std::string& path : all) {
    for (const cpa::Lang l : {cpa::Lang::Zh, cpa::Lang::En}) bad += edges(path, l);
  }
  TEST_ASSERT_TRUE_MESSAGE(bad.empty(), bad.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_script_draws_text_into_an_edge);
  return UNITY_END();
}
