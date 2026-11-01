// RecIndex (docs/11 §4.7): the committed segments waiting for sync, in id order, at most
// file_pending_max (256) entries of {id, size}, plus rev (0 until the boot recovery scan
// finished, then +1 on every commit, delete and recovery). About 4 KB, allocated once at
// boot. The audio task fills it during the scan; from IndexReady on only the loop changes
// it. A mutable container, like LineQueue: callers hold it in one place and never copy it.
// docs/28 S1a: an entry may be kept from one Mac (the ledger's AGAIN): excl points into a
// table of at most kHostSlots host hashes (0: none), so a Mac never lists what was taken
// back from it; a fifth Mac takes the oldest slot over and frees its entries. first: the
// entry is segment 0 of its recording (the plug-in hint counts recordings by it).
#pragma once

#include <cstdint>

#include "mono_id.h"

namespace cpa {
namespace rec {

constexpr uint16_t kIndexMax = 256;  // file_pending_max
constexpr uint8_t kHostSlots = 4;

class RecIndex {
 public:
  struct Entry {
    MonoId id;
    uint32_t size = 0;  // file size incl. the header, as served
    uint8_t excl = 0;   // 1..kHostSlots: never to that Mac
    bool first = false;
  };

  // false when full or already listed; excl_host 0: for every Mac
  bool insert(const MonoId& id, uint32_t size, uint32_t excl_host = 0, bool first = false);
  bool remove(const MonoId& id);                 // false when not listed
  const Entry* find(const MonoId& id) const;
  const Entry& at(uint16_t i) const { return entries_[i]; }
  uint16_t count() const { return n_; }
  bool full() const { return n_ >= kIndexMax; }
  bool ready() const { return rev_ > 0; }
  uint32_t rev() const { return rev_; }
  // The scan is done: rev becomes 1 + the recoveries it made.
  void set_ready(uint32_t recoveries) { rev_ = 1 + recoveries; }
  // The position of the first entry whose id text sorts after `after` (nullptr: 0).
  uint16_t first_after(const char* after) const;
  // docs/28: the entry is for this Mac (host 0, an unknown Mac, sees everything)
  bool for_host(const Entry& e, uint32_t host) const;
  uint8_t host_slot(uint32_t host);  // found or taken (1..kHostSlots); 0 for host 0
  uint8_t slot_for(uint32_t host) const;  // what host_slot would return, nothing changed
  uint32_t host_at(uint8_t slot) const { return slot >= 1 && slot <= kHostSlots ? hosts_[slot - 1] : 0; }
  uint16_t exclude_from(const MonoId& lo, uint32_t host);  // every entry >= lo; how many changed
  uint16_t held_from(uint32_t host) const;  // entries kept from host (0: none)
  uint16_t give_to(uint32_t host);          // ... for every Mac again; how many changed
  void touch() { bump(); }  // what a Mac may see changed: files.rev moves

 private:
  void bump() { rev_ = ready() ? rev_ + 1 : rev_; }

  Entry entries_[kIndexMax];
  uint16_t n_ = 0;
  uint32_t rev_ = 0;
  uint32_t hosts_[kHostSlots] = {};
  uint8_t next_evict_ = 0;
};

}  // namespace rec
}  // namespace cpa
