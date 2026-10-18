#pragma once
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/automation.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#define COMELIT_ISR_ATTR IRAM_ATTR
#include "decoder.h"

namespace esphome::comelit_simplebus {
class FrameTrigger : public Trigger<uint8_t, uint8_t, uint8_t> {};
struct RxStore {
  Decoder decoder;
  volatile uint32_t last_edge{0};
  volatile bool have_edge{false};
  static constexpr uint8_t SIZE = 32;
  volatile Record queue[SIZE]{};
  volatile uint8_t read{0}, write{0};
  volatile uint32_t dropped{0};
  static void gpio_intr(RxStore *arg);
};
class ComelitSimplebus : public Component {
 public:
  void set_rx_pin(InternalGPIOPin *pin) { rx_pin_ = pin; }
  void set_apartment_address(uint8_t address) { apartment_address_ = address; }
  void set_bus_monitor(bool enabled) { bus_monitor_ = enabled; }
  void set_ring_duration(uint32_t duration) { ring_duration_ = duration; }
  void set_ring(binary_sensor::BinarySensor *sensor) { ring_ = sensor; }
  void add_frame_trigger(FrameTrigger *trigger) { triggers_.push_back(trigger); }
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override { rx_pin_->detach_interrupt(); }
 protected:
  InternalGPIOPin *rx_pin_{nullptr};
  RxStore store_{};
  binary_sensor::BinarySensor *ring_{nullptr};
  std::vector<FrameTrigger *> triggers_;
  uint8_t apartment_address_{1};
  uint32_t ring_duration_{3000}, ring_started_{0}, last_dropped_{0};
  bool ringing_{false};
  bool bus_monitor_{false};
};
}  // namespace esphome::comelit_simplebus
