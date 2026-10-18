#include "comelit_simplebus.h"
#include "bus_monitor.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::comelit_simplebus {
static const char *const TAG = "comelit_simplebus";
void IRAM_ATTR RxStore::gpio_intr(RxStore *arg) {
  uint32_t now = micros();
  uint32_t previous = arg->last_edge;
  arg->last_edge = now;
  if (!arg->have_edge) { arg->have_edge = true; return; }
  Record r = arg->decoder.feed(now - previous);
  if (r.kind == NONE) return;
  uint8_t next = (arg->write + 1) % SIZE;
  if (next == arg->read) { arg->dropped++; return; }
  auto &dest = arg->queue[arg->write];
  dest.kind = r.kind; dest.state = r.state; dest.count = r.count;
  dest.bits = r.bits; dest.dt = r.dt;
  arg->write = next;
}
void ComelitSimplebus::setup() {
  rx_pin_->setup();
  if (ring_) ring_->publish_state(false);
  rx_pin_->attach_interrupt(RxStore::gpio_intr, &store_, gpio::INTERRUPT_ANY_EDGE);
}
void ComelitSimplebus::dump_config() {
  ESP_LOGCONFIG(TAG, "Comelit Simplebus2 TP28 receiver (RX only)");
  ESP_LOGCONFIG(TAG, "  Whole-bus event monitor: %s", bus_monitor_ ? "enabled" : "disabled");
  LOG_PIN("  RX pin: ", rx_pin_);
  ESP_LOGCONFIG(TAG, "  Address: %u; ring pulse: %u ms", apartment_address_, (unsigned) ring_duration_);
}
void ComelitSimplebus::loop() {
  // Bounded work: user automations cannot keep an unbounded drain loop alive.
  for (unsigned n = 0; n < RxStore::SIZE; ++n) {
    Record r;
    {
      InterruptLock lock;
      if (store_.read == store_.write) break;
      auto &src = store_.queue[store_.read];
      r.kind = src.kind; r.state = src.state; r.count = src.count;
      r.bits = src.bits; r.dt = src.dt;
      store_.read = (store_.read + 1) % RxStore::SIZE;
    }
    char raw[19];
    for (unsigned i = 0; i < r.count; ++i) raw[i] = (r.bits >> i) & 1U ? '1' : '0';
    raw[r.count] = 0;
    if (r.kind == REJECT) {
      ESP_LOGD(TAG, "REJECT state=%u bits=%u partial=%s dt=%u us", r.state, r.count, raw, (unsigned) r.dt);
      continue;
    }
    Frame f = decode(r.bits);
    ESP_LOGI(TAG, "RAW LSB: %.6s %.8s %.4s; command=%u address=%u checksum=%u calc=%u valid=%s",
             raw, raw + 6, raw + 14, f.command, f.address, f.checksum, f.calculated, f.valid ? "YES" : "NO");
    if (bus_monitor_) log_bus_frame(f.command, f.address, f.checksum, f.valid);
    if (!f.valid) continue;
    if (f.command == 50 && f.address == apartment_address_) {
      ring_started_ = millis();
      if (!ringing_) { ringing_ = true; if (ring_) ring_->publish_state(true); }
    }
    for (auto *trigger : triggers_) trigger->trigger(f.command, f.address, f.checksum);
  }
  if (ringing_ && uint32_t(millis() - ring_started_) >= ring_duration_) {
    ringing_ = false;
    if (ring_) ring_->publish_state(false);
  }
  uint32_t dropped;
  { InterruptLock lock; dropped = store_.dropped; }
  if (dropped != last_dropped_) {
    last_dropped_ = dropped;
    ESP_LOGW(TAG, "RX queue overflow: %u records dropped", (unsigned) dropped);
  }
}
}  // namespace esphome::comelit_simplebus
