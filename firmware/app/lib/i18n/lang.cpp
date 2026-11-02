#include "lang.h"

namespace cpa {
namespace {

Lang g_lang = Lang::En;  // until the shell reads NVS (v0.5.2: English first)

}  // namespace

Lang lang() { return g_lang; }

void set_lang(Lang l) { g_lang = l; }

}  // namespace cpa
