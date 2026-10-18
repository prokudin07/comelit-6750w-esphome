#pragma once

#include <Arduino.h>
#include "esphome.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR < 5
#error "Comelit TX requires ESP-IDF 5+ (Arduino-ESP32 3.x). Update ESPHome."
#endif
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "comelit_protocol.h"

namespace comelit {
constexpr gpio_num_t TX_PIN = GPIO_NUM_10;
constexpr uint32_t IDLE_US = 100000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 2000;
constexpr uint32_t TX_TIMEOUT_MS = 300;
constexpr uint32_t TX_INTERVAL_MS = 1000;

static uint8_t apartment_address = 1;
static uint32_t ring_hold_ms = 3000;
static bool ringing = false;
static uint32_t ring_started_ms = 0;
static void (*ring_callback)(bool) = nullptr;
static void (*status_callback)(const char *) = nullptr;
static bool tx_enabled = false;
static bool tx_ready = false;
static bool channel_enabled = false;
static bool pending = false;
static bool transmitting = false;
static uint8_t pending_command = 0;
static uint8_t active_command = 0;
static uint32_t requested_ms = 0;
static uint32_t last_started_ms = 0;
static rmt_channel_handle_t channel = nullptr;
static rmt_encoder_handle_t encoder = nullptr;
// Persistent storage: rmt_transmit retains this buffer until completion.
static rmt_symbol_word_t symbols[20]{};

inline void status(const char *message) {
  ESP_LOGI("comelit", "%s", message);
  if (status_callback) status_callback(message);
}

inline void early_off() {
  gpio_set_level(TX_PIN, 0);
  gpio_set_direction(TX_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(TX_PIN, 0);
}

inline void shutdown() {
  pending = false;
  transmitting = false;
  tx_enabled = false;
  tx_ready = false;
  if (channel && channel_enabled) {
    const esp_err_t err = rmt_disable(channel);
    if (err != ESP_OK) ESP_LOGE("comelit", "rmt_disable: %s", esp_err_to_name(err));
    channel_enabled = false;
  }
  if (encoder) {
    const esp_err_t err = rmt_del_encoder(encoder);
    if (err == ESP_OK) encoder = nullptr;
    else ESP_LOGE("comelit", "rmt_del_encoder: %s", esp_err_to_name(err));
  }
  if (channel) {
    const esp_err_t err = rmt_del_channel(channel);
    if (err == ESP_OK) channel = nullptr;
    else ESP_LOGE("comelit", "rmt_del_channel: %s", esp_err_to_name(err));
  }
  gpio_reset_pin(TX_PIN);
  early_off();
}

inline bool init_tx() {
  if (tx_ready) return true;
  if (channel || encoder) {
    status("Ошибка освобождения RMT; перезагрузите ESP");
    return false;
  }
  early_off();
  rmt_tx_channel_config_t cfg{};
  cfg.gpio_num = TX_PIN;
  cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  cfg.resolution_hz = 1000000;
  cfg.mem_block_symbols = 48;  // ESP32-C3: all 20 symbols fit in hardware memory.
  cfg.trans_queue_depth = 1;
  esp_err_t err = rmt_new_tx_channel(&cfg, &channel);
  if (err == ESP_OK) {
    rmt_copy_encoder_config_t ecfg{};
    err = rmt_new_copy_encoder(&ecfg, &encoder);
  }
  if (err == ESP_OK) {
    rmt_carrier_config_t carrier{};
    carrier.frequency_hz = 25000;
    carrier.duty_cycle = 0.20f;
    err = rmt_apply_carrier(channel, &carrier);
  }
  if (err == ESP_OK) {
    err = rmt_enable(channel);
    channel_enabled = err == ESP_OK;
  }
  if (err == ESP_OK) err = gpio_set_drive_capability(TX_PIN, GPIO_DRIVE_CAP_2);
  if (err != ESP_OK) {
    ESP_LOGE("comelit", "TX init failed: %s", esp_err_to_name(err));
    shutdown();
    status("Ошибка инициализации TX; RX продолжает работать");
    return false;
  }
  last_started_ms = millis(); // One second after setup before the first TX.
  return tx_ready = true;
}

inline void setup(uint16_t address, uint32_t hold_ms,
                  void (*on_ring)(bool), void (*on_status)(const char *)) {
  ring_callback = on_ring;
  status_callback = on_status;
  ringing = false;
  if (ring_callback) ring_callback(false);
  tx_enabled = false;
  pending = transmitting = false;
  if (address > 255 || hold_ms == 0 || hold_ms > 60000) {
    status("Ошибка адреса или длительности сенсора");
    return;
  }
  apartment_address = uint8_t(address);
  ring_hold_ms = hold_ms;
  // RX works even with the permission switch off or without a TX circuit.
  if (init_tx()) status("Готово; передача выключена");
}

inline bool set_enabled(bool enabled) {
  if (!enabled) {
    shutdown(); // Also stops active transmission and clears pending requests.
    status("Передача выключена");
    return true;
  }
  if (!init_tx()) return false;
  tx_enabled = true;
  status("Передача разрешена");
  return true;
}

// Called only from the existing RX main-loop frame processor, never the ISR.
inline void on_frame(uint8_t command, uint8_t address, bool valid) {
  if (!valid || address != apartment_address || command != 50) return;
  ring_started_ms = millis();
  if (!ringing) {
    ringing = true;
    if (ring_callback) ring_callback(true);
  }
}

// One queued command, no automatic retries or replay after reconnection.
inline bool request(uint8_t command) {
  if (command != 16 && command != 17 && command != 18) return false;
  if (!tx_enabled || !tx_ready) {
    status("Не отправлено: включите разрешение TX");
    return false;
  }
  if (pending || transmitting) {
    status("Новая команда отклонена: предыдущая ещё выполняется");
    return false;
  }
  pending_command = command;
  requested_ms = millis();
  pending = true;
  status("Команда ожидает свободную шину");
  return true;
}

inline bool answer() { return request(17); }
inline bool hang_up() { return request(18); }
inline bool open_door() { return request(16); }

// Call every 10 ms. RX timestamp comes from the original ISR; no RX reconfiguration.
inline void process(uint32_t rx_last_edge_us, bool rx_idle_high) {
  const uint32_t now = millis();
  if (ringing && uint32_t(now - ring_started_ms) >= ring_hold_ms) {
    ringing = false;
    if (ring_callback) ring_callback(false);
  }
  if (transmitting) {
    const esp_err_t err = rmt_tx_wait_all_done(channel, 0); // Never blocks ESPHome.
    if (err == ESP_OK) {
      transmitting = false;
      char message[128];
      snprintf(message, sizeof(message), "Отправлен пакет %u на адрес %u; ACK не проверен",
               active_command, apartment_address);
      status(message);
    } else if (err != ESP_ERR_TIMEOUT || uint32_t(now - last_started_ms) >= TX_TIMEOUT_MS) {
      shutdown();
      status("Ошибка TX: выход выключен; требуется повторное разрешение");
    }
    return;
  }
  if (!pending) return;
  if (uint32_t(now - requested_ms) >= REQUEST_TIMEOUT_MS) {
    pending = false;
    status("Не отправлено: шина не освободилась за 2 секунды");
    return;
  }
  if (!tx_enabled || !tx_ready) { pending = false; return; }
  if (uint32_t(now - last_started_ms) < TX_INTERVAL_MS || !rx_idle_high) return;
  if (uint32_t(micros() - rx_last_edge_us) < IDLE_US) return;

  simplebus::Frame frame{};
  if (!simplebus::encode(pending_command, apartment_address, frame)) {
    pending = false;
    status("Неверная команда или адрес");
    return;
  }
  for (unsigned i = 0; i < 20; ++i) {
    symbols[i].duration0 = frame.symbols[i].high_us;
    symbols[i].level0 = 1;
    symbols[i].duration1 = frame.symbols[i].low_us;
    symbols[i].level1 = 0;
  }
  active_command = pending_command;
  pending = false;
  rmt_transmit_config_t cfg{};
  cfg.loop_count = 0;
  cfg.flags.eot_level = 0;
  const esp_err_t err = rmt_transmit(channel, encoder, symbols, sizeof(symbols), &cfg);
  if (err != ESP_OK) {
    ESP_LOGE("comelit", "rmt_transmit: %s", esp_err_to_name(err));
    shutdown();
    status("Ошибка запуска TX; повторов не будет");
    return;
  }
  last_started_ms = now;
  transmitting = true;
  // No log between the idle check and rmt_transmit: keep that gap short.
}
} // namespace comelit
