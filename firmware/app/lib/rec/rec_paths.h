// Recording segment paths on the card (docs/11 §4.4), below the store root (/cpa, or a
// test root such as /cpatest):
//   <root>/rec/<E7>/<B5><S first 3>.W<S last>   the WAV of segment <E7>-<B5>-<S4>
//   <root>/rec/<E7>/<B5><S first 3>.M<S last>   its .M commit marker
// Upper-case Crockford digits are valid 8.3 characters, so FatFs writes no long-name
// entries; one directory per epoch, like the outbox (09 §3.5).
#pragma once

#include "fixed_str.h"
#include "mono_id.h"
#include "opt.h"

namespace cpa {
namespace rec {

using RecPath = FixedStr<40>;

constexpr uint8_t kMaxEpochs = 8;  // epoch directories scanned (more wait: never in practice)

struct RecName {
  MonoId id;
  bool wav = false;  // .W (else .M)
};

RecPath rec_dir(const char* root);
RecPath rec_epoch_dir(const char* root, const MonoId& id);
RecPath rec_wav_path(const char* root, const MonoId& id);
RecPath rec_meta_path(const char* root, const MonoId& id);
bool rec_is_epoch_dir(const char* name);  // 7 Crockford digits that fit 32 bits
Opt<RecName> rec_parse_name(const char* epoch, const char* name);
// docs/28 S1a: <root>/rec/<E7>/SENT.L (which 0) or SENT.K (1)
RecPath rec_ledger_path(const char* root, uint32_t epoch, uint8_t which);

}  // namespace rec
}  // namespace cpa
