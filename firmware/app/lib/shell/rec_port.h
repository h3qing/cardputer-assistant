// RecPort (docs/11 §4.7): the loop's side of the two queues to the audio task. hal's
// AudioTask implements it with FreeRTOS queues; tests use FakeRecPort, which runs a
// RecEngine on the host. Never blocks.
#pragma once

#include "rec_health.h"
#include "rec_index.h"
#include "rec_msgs.h"

namespace cpa {

class RecPort {
 public:
  virtual ~RecPort() = default;
  virtual bool post(const rec::RecCmd& c) = 0;  // false: the command queue is full
  virtual uint8_t room() const = 0;              // free slots in the command queue
  virtual bool poll(rec::RecEvt& e) = 0;        // the next event, if any
  // Items / Data payloads: valid from their event until the next file command is posted.
  virtual const rec::TransferBuf& xfer() const = 0;
  // Filled by the audio task's boot scan; the loop's alone once IndexReady arrived.
  virtual rec::RecIndex& index() = 0;
  // Test firmware (debug.state, gh #21): the latest health counters the task measured
  // (not measured by default), and its stack never used so far in bytes (0: no task).
  virtual rec::RecHealth health() const { return rec::RecHealth{}; }
  virtual uint32_t stack_left() const { return 0; }
};

// What setup() learned for recording (docs/11 §4.3, §4.7).
struct RecBoot {
  bool buffers = false;     // the ring and index were allocated (only with the SD card primary)
  bool free_known = false;  // the bounded FSINFO read worked
  uint64_t free_bytes = 0;
};

}  // namespace cpa
