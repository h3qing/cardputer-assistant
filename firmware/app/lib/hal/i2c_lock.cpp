#include "i2c_lock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

namespace cpa {
namespace {

SemaphoreHandle_t s_lock = nullptr;
std::atomic<bool> s_want{false};

void take() {
  if (CPA_I2C_LOCK && s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

void give() {
  if (CPA_I2C_LOCK && s_lock) xSemaphoreGive(s_lock);
}

}  // namespace

void hal_i2c_lock_begin() {
  if (CPA_I2C_LOCK && !s_lock) s_lock = xSemaphoreCreateMutex();
}

void hal_i2c_loop_take() { take(); }

void hal_i2c_loop_give() {
  give();
  if (s_want.load()) vTaskDelay(1);  // hand the lock to the audio task on core 0
}

void hal_i2c_audio_take() {
  s_want.store(true);
  take();
  s_want.store(false);
}

void hal_i2c_audio_give() { give(); }

}  // namespace cpa
