// Runtime: the settings NVS keeps (docs/21 §6.8). Read once at boot, before anything is
// drawn; a switch is written only when it differs from what NVS holds, never while
// recording (a flash write stalls the cache) but as soon as the recording has ended, and
// once: a failed write is said, not retried every pass.
#include "lang_switch.h"
#include "runtime.h"

namespace cpa {

void Runtime::begin_settings() {
  settings_ = plat_.load_settings();
  stored_ = settings_;
  settings_due_ = false;
  set_lang(settings_.lang);
}

SettingsSave Runtime::store_lang(Lang l) {
  settings_ = with_lang(settings_, l);
  const SaveWhen when = save_when(settings_, stored_, rec::rec_live(rec_core_));
  settings_due_ = when == SaveWhen::AfterRec;
  if (when == SaveWhen::Never) return SettingsSave::Saved;
  return settings_due_ ? SettingsSave::Pending : write_settings();
}

SettingsSave Runtime::write_settings() {
  if (!plat_.save_settings(settings_)) return SettingsSave::Failed;
  stored_ = settings_;
  return SettingsSave::Saved;
}

void Runtime::sync_settings() {
  if (rec::rec_live(rec_core_)) return;
  settings_due_ = false;
  if (settings_ == stored_ || write_settings() == SettingsSave::Saved) return;
  shell_.toast(ToastLevel::Warn, lang_unsaved_text(stored_.lang).c_str());
}

}  // namespace cpa
