#include "nvs_boot.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>

namespace cpa {
namespace {

constexpr char kNamespace[] = "cpa";
constexpr char kEpoch[] = "epoch";
constexpr char kBoot[] = "boot";

}  // namespace

BootRecord nvs_boot_read() {
  Preferences prefs;
  BootRecord rec;
  if (!prefs.begin(kNamespace, true)) return rec;
  rec.epoch = prefs.getUInt(kEpoch, 0);
  rec.boot = prefs.getUInt(kBoot, 0);
  prefs.end();
  return rec;
}

BootTicket nvs_boot_ticket() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return BootTicket{};
  const bool known = prefs.isKey(kEpoch);
  const BootRecord stored = known ? BootRecord{prefs.getUInt(kEpoch, 0), prefs.getUInt(kBoot, 0)}
                                  : boot_init(epoch_mix(esp_random(), ESP.getEfuseMac()));
  const auto advanced = boot_advance(stored);
  if (!advanced.ok) {
    prefs.end();
    return BootTicket{};
  }
  const bool wrote = prefs.putUInt(kEpoch, advanced.value.epoch) == sizeof(uint32_t) &&
                     prefs.putUInt(kBoot, advanced.value.boot) == sizeof(uint32_t);
  prefs.end();
  const BootRecord back = nvs_boot_read();
  const auto ticket = wrote ? boot_ticket(advanced.value, back) : Opt<BootTicket>{};
  return ticket.has ? ticket.value : BootTicket{};
}

}  // namespace cpa
