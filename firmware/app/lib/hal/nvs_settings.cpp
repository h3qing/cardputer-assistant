#include "nvs_settings.h"

#include <Preferences.h>

namespace cpa {
namespace {

constexpr char kNamespace[] = "cpa";
constexpr char kLang[] = "lang";  // 0 中文, 1 English (lang_from_nvs)
constexpr char kBoot[] = "boot";  // nvs_boot.cpp's counter: an earlier boot wrote it
constexpr char kMarkBoot[] = "offb";
constexpr char kMarkSeq[] = "offs";

bool put_u32(Preferences& p, const char* key, uint32_t v) {
  const bool same = p.isKey(key) && p.getULong(key, ~v) == v;
  return (same || p.putULong(key, v) == sizeof(uint32_t)) && p.getULong(key, ~v) == v;
}

}  // namespace

Settings nvs_settings_load() {
  Preferences prefs;
  Settings s;
  if (!prefs.begin(kNamespace, true)) return s;
  s.lang = lang_from_nvs(prefs.getUChar(kLang, lang_to_nvs(s.lang)));
  prefs.end();
  return s;
}

bool nvs_settings_save(const Settings& s) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const uint8_t b = lang_to_nvs(s.lang);
  const bool same = prefs.isKey(kLang) && prefs.getUChar(kLang, 0xFF) == b;
  const bool wrote = same || prefs.putUChar(kLang, b) == sizeof(uint8_t);
  const bool back = wrote && prefs.getUChar(kLang, 0xFF) == b;
  prefs.end();
  return back;
}

bool nvs_lang_migrate() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const Opt<Lang> first = lang_to_store(prefs.isKey(kLang), prefs.isKey(kBoot));
  const bool wrote = first.has && prefs.putUChar(kLang, lang_to_nvs(first.value)) == sizeof(uint8_t);
  prefs.end();
  return wrote;
}

Opt<OfflineMark> nvs_offline_mark_load() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return {};
  const bool has = prefs.isKey(kMarkBoot) && prefs.isKey(kMarkSeq);
  OfflineMark m;
  m.boot = prefs.getULong(kMarkBoot, 0);
  m.seq = prefs.getULong(kMarkSeq, 0);
  prefs.end();
  return has ? some(m) : Opt<OfflineMark>{};
}

bool nvs_offline_mark_save(const OfflineMark& m) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = put_u32(prefs, kMarkBoot, m.boot) && put_u32(prefs, kMarkSeq, m.seq);
  prefs.end();
  return ok;
}

}  // namespace cpa
