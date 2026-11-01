#include "lang.h"

namespace cpa {
namespace {

Lang g_lang = Lang::Zh;

}  // namespace

Lang lang() { return g_lang; }

void set_lang(Lang l) { g_lang = l; }

}  // namespace cpa
