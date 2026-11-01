// Module registration (05 §5.5): a plain static list. No self-registering static
// constructors: the linker drops unreferenced objects from library archives and the
// initialisation order would be unspecified. The shell checks the ids at start-up.
// v0.4.0 (docs/13 §5.5): home is registered, so the shell makes it the root page
// (Opt+H, back from an app's first level); the board moved to Opt+P.
#include "module_list.h"

#include "board_module.h"
#include "home_module.h"
#include "inbox_module.h"
#include "system_module.h"

namespace cpa {
namespace {

HomeModule g_home;
BoardModule g_board;
InboxModule g_inbox;
SystemModule g_system;

}  // namespace

Module* const kModules[] = {&g_home, &g_board, &g_inbox, &g_system};
const uint8_t kModuleCount = sizeof(kModules) / sizeof(kModules[0]);

}  // namespace cpa
