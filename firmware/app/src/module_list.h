// The statically registered modules (05 §5.5). Adding a module = a directory under
// modules/<id>/ plus one line in module_list.cpp.
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

extern Module* const kModules[];
extern const uint8_t kModuleCount;

}  // namespace cpa
