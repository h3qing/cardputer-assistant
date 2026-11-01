#include "rec_index.h"

#include <cstring>

namespace cpa {
namespace rec {

bool RecIndex::insert(const MonoId& id, uint32_t size, uint32_t excl_host, bool first) {
  if (full() || find(id)) return false;
  const uint8_t excl = host_slot(excl_host);
  uint16_t i = n_;
  while (i > 0 && id < entries_[i - 1].id) {
    entries_[i] = entries_[i - 1];
    --i;
  }
  entries_[i].id = id;
  entries_[i].size = size;
  entries_[i].excl = excl;
  entries_[i].first = first;
  ++n_;
  bump();
  return true;
}

bool RecIndex::remove(const MonoId& id) {
  for (uint16_t i = 0; i < n_; ++i) {
    if (entries_[i].id != id) continue;
    for (uint16_t k = i + 1; k < n_; ++k) entries_[k - 1] = entries_[k];
    --n_;
    bump();
    return true;
  }
  return false;
}

const RecIndex::Entry* RecIndex::find(const MonoId& id) const {
  for (uint16_t i = 0; i < n_; ++i) {
    if (entries_[i].id == id) return &entries_[i];
  }
  return nullptr;
}

uint16_t RecIndex::first_after(const char* after) const {
  if (!after) return 0;
  uint16_t i = 0;
  while (i < n_ && std::strcmp(mono_format(entries_[i].id).c_str(), after) <= 0) ++i;
  return i;
}

bool RecIndex::for_host(const Entry& e, uint32_t host) const {
  return host == 0 || e.excl == 0 || host_at(e.excl) != host;
}

uint8_t RecIndex::slot_for(uint32_t host) const {
  if (host == 0) return 0;
  for (uint8_t i = 0; i < kHostSlots; ++i) {
    if (hosts_[i] == host) return static_cast<uint8_t>(i + 1);
  }
  for (uint8_t i = 0; i < kHostSlots; ++i) {
    if (hosts_[i] == 0) return static_cast<uint8_t>(i + 1);
  }
  return static_cast<uint8_t>(next_evict_ + 1);  // the oldest taken over
}

uint8_t RecIndex::host_slot(uint32_t host) {
  const uint8_t slot = slot_for(host);
  if (slot == 0 || hosts_[slot - 1] == host) return slot;
  if (hosts_[slot - 1] != 0) {  // taken over: what pointed at it is for every Mac now
    next_evict_ = static_cast<uint8_t>((next_evict_ + 1) % kHostSlots);
    for (uint16_t k = 0; k < n_; ++k) {
      if (entries_[k].excl == slot) entries_[k].excl = 0;
    }
  }
  hosts_[slot - 1] = host;
  return slot;
}

uint16_t RecIndex::exclude_from(const MonoId& lo, uint32_t host) {
  const uint8_t slot = host_slot(host);
  uint16_t changed = 0;
  for (uint16_t i = 0; i < n_ && slot != 0; ++i) {
    if (entries_[i].id < lo || entries_[i].excl == slot) continue;
    entries_[i].excl = slot;
    ++changed;
  }
  if (changed > 0) bump();
  return changed;
}

uint16_t RecIndex::held_from(uint32_t host) const {
  uint16_t n = 0;
  for (uint16_t i = 0; i < n_ && host != 0; ++i) n = static_cast<uint16_t>(n + (for_host(entries_[i], host) ? 0 : 1));
  return n;
}

uint16_t RecIndex::give_to(uint32_t host) {
  uint16_t changed = 0;
  for (uint16_t i = 0; i < n_ && host != 0; ++i) {
    if (for_host(entries_[i], host)) continue;
    entries_[i].excl = 0;
    ++changed;
  }
  if (changed > 0) bump();
  return changed;
}

}  // namespace rec
}  // namespace cpa
