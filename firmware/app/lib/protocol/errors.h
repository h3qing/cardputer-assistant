// Wire names of the protocol v0 error codes (05 §6.8, common.schema.json errorCode).
#pragma once

#include <cstddef>

#include "err_code.h"
#include "opt.h"

namespace cpa {

const char* err_name(ErrCode code);  // "" for ErrCode::None
Opt<ErrCode> err_parse(const char* s, size_t n);

}  // namespace cpa
