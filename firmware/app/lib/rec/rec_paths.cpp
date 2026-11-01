#include "rec_paths.h"

#include <cstring>

#include "ledger_rec.h"

namespace cpa {
namespace rec {
namespace {

constexpr size_t kEpochDigits = 7;
constexpr size_t kNameLen = 11;  // BBBBBSSS.XS

RecPath stem(const char* root, const MonoId& id) {
  const MonoIdStr s = mono_format(id);  // EEEEEEE-BBBBB-SSSS
  return rec_epoch_dir(root, id).append("/").append(s.c_str() + 8, 5).append(s.c_str() + 14, 3);
}

char last_digit(const MonoId& id) { return mono_format(id).c_str()[17]; }

RecPath with_ext(const char* root, const MonoId& id, char kind) {
  const char ext[4] = {'.', kind, last_digit(id), '\0'};
  return stem(root, id).append(ext);
}

}  // namespace

RecPath rec_dir(const char* root) { return RecPath::from(root).append("/rec"); }

RecPath rec_epoch_dir(const char* root, const MonoId& id) {
  return rec_dir(root).append("/").append(mono_format(id).c_str(), kEpochDigits);
}

RecPath rec_wav_path(const char* root, const MonoId& id) { return with_ext(root, id, 'W'); }
RecPath rec_meta_path(const char* root, const MonoId& id) { return with_ext(root, id, 'M'); }

bool rec_is_epoch_dir(const char* name) {
  const Opt<uint64_t> v = std::strlen(name) == kEpochDigits ? crockford_decode(name, kEpochDigits) : Opt<uint64_t>{};
  return v.has && v.value <= 0xFFFFFFFFull;
}

Opt<RecName> rec_parse_name(const char* epoch, const char* name) {
  if (!rec_is_epoch_dir(epoch) || std::strlen(name) != kNameLen || name[8] != '.') return {};
  if (name[9] != 'W' && name[9] != 'M') return {};
  char text[kMonoIdLen + 1] = {};
  std::memcpy(text, epoch, kEpochDigits);
  text[7] = '-';
  std::memcpy(text + 8, name, 5);
  text[13] = '-';
  std::memcpy(text + 14, name + 5, 3);
  text[17] = name[10];
  const Opt<MonoId> id = mono_parse(text, kMonoIdLen);
  if (!id.has) return {};
  RecName out;
  out.id = id.value;
  out.wav = name[9] == 'W';
  return some(out);
}

RecPath rec_ledger_path(const char* root, uint32_t epoch, uint8_t which) {
  MonoId id;
  id.epoch = epoch;
  return rec_epoch_dir(root, id).append("/").append(ledger_name(which));
}

}  // namespace rec
}  // namespace cpa
