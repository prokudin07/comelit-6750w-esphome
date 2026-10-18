#pragma once

#include <Arduino.h>
#include "esphome.h"

// Comelit Simplebus2 decoder + REJECT diagnostics
// Actual RX point: TP28 -> 10k -> ESP32-C3 GPIO3
// Historical filename retained for compatibility.

static const uint8_t RX_PIN = 3;
static const uint32_t CARRIER_MIN_US = 3000;
static const uint32_t CARRIER_MAX_US = 3800;
static const uint32_t PREAMBLE_MIN_US = 14500;
static const uint32_t PREAMBLE_MAX_US = 19000;
static const uint32_t BIT0_MIN_US = 2300;
static const uint32_t BIT0_MAX_US = 3500;
static const uint32_t BIT1_MIN_US = 5000;
static const uint32_t BIT1_MAX_US = 7000;
static const uint32_t MIN_SIGNIFICANT_INTERVAL_US = 2000;

enum DecodeState : uint8_t {
  WAIT_CARRIER = 0,
  WAIT_PREAMBLE = 1,
  WAIT_BIT_CARRIER = 2,
  WAIT_BIT_GAP = 3
};

volatile DecodeState decode_state = WAIT_CARRIER;
volatile uint32_t last_edge_us = 0;
volatile uint8_t working_bits[18];
volatile uint8_t working_count = 0;

static const uint8_t FRAME_QUEUE_SIZE = 8;
struct SimplebusFrame { uint8_t bits[18]; };
volatile SimplebusFrame frame_queue[FRAME_QUEUE_SIZE];
volatile uint8_t queue_write = 0;
volatile uint8_t queue_read = 0;
volatile uint32_t good_frames = 0;
volatile uint32_t rejected_sequences = 0;
volatile uint32_t dropped_frames = 0;

static const uint8_t REJECT_QUEUE_SIZE = 16;
struct RejectRecord {
  uint8_t reason;      // 1 PREAMBLE, 2 BIT_CARRIER, 3 BIT_GAP
  uint8_t state;
  uint8_t bit_count;
  uint32_t dt;
  uint8_t bits[18];
};
volatile RejectRecord reject_queue[REJECT_QUEUE_SIZE];
volatile uint8_t reject_write = 0;
volatile uint8_t reject_read = 0;
volatile uint32_t dropped_rejects = 0;

static uint32_t total_valid_frames = 0;
static uint32_t total_invalid_frames = 0;
static uint32_t apartment1_calls = 0;
static uint32_t command_count[64] = {0};
static uint8_t command_last_address[64] = {0};

inline bool IRAM_ATTR in_range(uint32_t value, uint32_t min_value, uint32_t max_value) {
  return value >= min_value && value <= max_value;
}

void IRAM_ATTR reset_decoder() {
  decode_state = WAIT_CARRIER;
  working_count = 0;
}

void IRAM_ATTR store_reject(uint8_t reason, uint32_t dt) {
  rejected_sequences++;
  uint8_t next = (reject_write + 1) % REJECT_QUEUE_SIZE;
  if (next == reject_read) {
    dropped_rejects++;
    return;
  }
  reject_queue[reject_write].reason = reason;
  reject_queue[reject_write].state = (uint8_t) decode_state;
  reject_queue[reject_write].bit_count = working_count;
  reject_queue[reject_write].dt = dt;
  for (uint8_t i = 0; i < 18; i++)
    reject_queue[reject_write].bits[i] = (i < working_count) ? working_bits[i] : 0;
  reject_write = next;
}

void IRAM_ATTR store_frame() {
  uint8_t next = (queue_write + 1) % FRAME_QUEUE_SIZE;
  if (next == queue_read) {
    dropped_frames++;
    reset_decoder();
    return;
  }
  for (uint8_t i = 0; i < 18; i++) frame_queue[queue_write].bits[i] = working_bits[i];
  queue_write = next;
  good_frames++;
  reset_decoder();
}

void IRAM_ATTR tp29_isr() {
  uint32_t now = micros();
  uint32_t previous = last_edge_us;
  last_edge_us = now;
  if (previous == 0) return;

  uint32_t dt = now - previous;
  if (dt < MIN_SIGNIFICANT_INTERVAL_US) return;

  switch (decode_state) {
    case WAIT_CARRIER:
      if (in_range(dt, CARRIER_MIN_US, CARRIER_MAX_US)) decode_state = WAIT_PREAMBLE;
      break;

    case WAIT_PREAMBLE:
      if (in_range(dt, PREAMBLE_MIN_US, PREAMBLE_MAX_US)) {
        working_count = 0;
        decode_state = WAIT_BIT_CARRIER;
      } else if (in_range(dt, CARRIER_MIN_US, CARRIER_MAX_US)) {
        decode_state = WAIT_PREAMBLE;
      } else {
        store_reject(1, dt);
        reset_decoder();
      }
      break;

    case WAIT_BIT_CARRIER:
      if (in_range(dt, CARRIER_MIN_US, CARRIER_MAX_US)) {
        decode_state = WAIT_BIT_GAP;
      } else if (in_range(dt, PREAMBLE_MIN_US, PREAMBLE_MAX_US)) {
        working_count = 0;
        decode_state = WAIT_BIT_CARRIER;
      } else {
        store_reject(2, dt);
        reset_decoder();
      }
      break;

    case WAIT_BIT_GAP: {
      uint8_t bit;
      if (in_range(dt, BIT0_MIN_US, BIT0_MAX_US)) bit = 0;
      else if (in_range(dt, BIT1_MIN_US, BIT1_MAX_US)) bit = 1;
      else {
        store_reject(3, dt);
        reset_decoder();
        break;
      }

      if (working_count < 18) working_bits[working_count++] = bit;
      if (working_count == 18) store_frame();
      else decode_state = WAIT_BIT_CARRIER;
      break;
    }
  }
}

void tp29_sniffer_setup() {
  pinMode(RX_PIN, INPUT);
  last_edge_us = 0;
  queue_write = queue_read = 0;
  reject_write = reject_read = 0;
  good_frames = rejected_sequences = dropped_frames = dropped_rejects = 0;
  total_valid_frames = total_invalid_frames = apartment1_calls = 0;
  for (uint8_t i = 0; i < 64; i++) {
    command_count[i] = 0;
    command_last_address[i] = 0;
  }
  reset_decoder();
  attachInterrupt(digitalPinToInterrupt(RX_PIN), tp29_isr, CHANGE);
  ESP_LOGI("simplebus", "Comelit Simplebus2 diagnostic decoder");
  ESP_LOGI("simplebus", "RX: TP28 -> GPIO%d", RX_PIN);
  ESP_LOGI("simplebus", "REJECT diagnostics ENABLED");
}

void tp29_process_rejects() {
  while (true) {
    RejectRecord rec;
    bool have_reject = false;
    noInterrupts();
    if (reject_read != reject_write) {
      rec.reason = reject_queue[reject_read].reason;
      rec.state = reject_queue[reject_read].state;
      rec.bit_count = reject_queue[reject_read].bit_count;
      rec.dt = reject_queue[reject_read].dt;
      for (uint8_t i = 0; i < 18; i++) rec.bits[i] = reject_queue[reject_read].bits[i];
      reject_read = (reject_read + 1) % REJECT_QUEUE_SIZE;
      have_reject = true;
    }
    interrupts();
    if (!have_reject) break;

    const char *reason_text = rec.reason == 1 ? "PREAMBLE" : rec.reason == 2 ? "BIT_CARRIER" : rec.reason == 3 ? "BIT_GAP" : "UNKNOWN";
    const char *state_text = rec.state == WAIT_CARRIER ? "WAIT_CARRIER" : rec.state == WAIT_PREAMBLE ? "WAIT_PREAMBLE" : rec.state == WAIT_BIT_CARRIER ? "WAIT_BIT_CARRIER" : rec.state == WAIT_BIT_GAP ? "WAIT_BIT_GAP" : "UNKNOWN";

    char partial[20];
    uint8_t n = rec.bit_count > 18 ? 18 : rec.bit_count;
    for (uint8_t i = 0; i < n; i++) partial[i] = rec.bits[i] ? '1' : '0';
    partial[n] = '\0';

    ESP_LOGW("simplebus", "!!!!!!!!!! REJECT !!!!!!!!!!");
    ESP_LOGW("simplebus", "REASON    : %s", reason_text);
    ESP_LOGW("simplebus", "STATE     : %s", state_text);
    ESP_LOGW("simplebus", "BITS      : %u / 18", rec.bit_count);
    ESP_LOGW("simplebus", "PARTIAL   : %s", rec.bit_count > 0 ? partial : "(none)");
    ESP_LOGW("simplebus", "BAD dt    : %lu us", (unsigned long) rec.dt);
    ESP_LOGW("simplebus", "REJECTS   : %lu", (unsigned long) rejected_sequences);
    ESP_LOGW("simplebus", "DROPPED R : %lu", (unsigned long) dropped_rejects);
  }
}

void tp29_process_frames() {
  while (true) {
    uint8_t bits[18];
    bool have_frame = false;
    noInterrupts();
    if (queue_read != queue_write) {
      for (uint8_t i = 0; i < 18; i++) bits[i] = frame_queue[queue_read].bits[i];
      queue_read = (queue_read + 1) % FRAME_QUEUE_SIZE;
      have_frame = true;
    }
    interrupts();
    if (!have_frame) break;

    uint8_t command = 0, address = 0, checksum = 0;
    for (uint8_t i = 0; i < 6; i++) if (bits[i]) command |= (1U << i);
    for (uint8_t i = 0; i < 8; i++) if (bits[6 + i]) address |= (1U << i);
    for (uint8_t i = 0; i < 4; i++) if (bits[14 + i]) checksum |= (1U << i);

    uint8_t calculated_checksum = 0;
    for (uint8_t i = 0; i < 14; i++) calculated_checksum += bits[i];
    calculated_checksum &= 0x0F;
    bool valid = checksum == calculated_checksum;

    if (valid) {
      total_valid_frames++;
      command_count[command]++;
      command_last_address[command] = address;
    } else total_invalid_frames++;

    bool our_call = valid && command == 50 && address == 1;
    if (our_call) {
      apartment1_calls++;
      ESP_LOGI("simplebus", "*** CALL TO APARTMENT 1 !!! COUNT=%lu ***", (unsigned long) apartment1_calls);
    }

    char command_bits[7], address_bits[9], checksum_bits[5], raw[32];
    for (uint8_t i = 0; i < 6; i++) command_bits[i] = bits[i] ? '1' : '0';
    command_bits[6] = '\0';
    for (uint8_t i = 0; i < 8; i++) address_bits[i] = bits[6+i] ? '1' : '0';
    address_bits[8] = '\0';
    for (uint8_t i = 0; i < 4; i++) checksum_bits[i] = bits[14+i] ? '1' : '0';
    checksum_bits[4] = '\0';
    snprintf(raw, sizeof(raw), "%s %s %s", command_bits, address_bits, checksum_bits);

    ESP_LOGI("simplebus", "========== SIMPLEBUS FRAME ==========");
    ESP_LOGI("simplebus", "RAW LSB   : %s", raw);
    ESP_LOGI("simplebus", "COMMAND   : %u (0x%02X)", command, command);
    ESP_LOGI("simplebus", "ADDRESS   : %u (0x%02X)", address, address);
    ESP_LOGI("simplebus", "CHECKSUM  : %u", checksum);
    ESP_LOGI("simplebus", "CALC CS   : %u", calculated_checksum);
    ESP_LOGI("simplebus", "VALID     : %s", valid ? "YES" : "NO");
    ESP_LOGI("simplebus", "TOTAL     : valid=%lu invalid=%lu calls=%lu",
      (unsigned long) total_valid_frames, (unsigned long) total_invalid_frames, (unsigned long) apartment1_calls);
    ESP_LOGI("simplebus", "ISR       : frames=%lu rejected=%lu dropped=%lu",
      (unsigned long) good_frames, (unsigned long) rejected_sequences, (unsigned long) dropped_frames);

    ESP_LOGI("simplebus", "COMMAND STATISTICS:");
    for (uint8_t cmd = 0; cmd < 64; cmd++) {
      if (command_count[cmd] == 0) continue;
      ESP_LOGI("simplebus", "CMD %2u (0x%02X): %lu times, last addr=%u",
        cmd, cmd, (unsigned long) command_count[cmd], command_last_address[cmd]);
    }
  }
}

void tp29_sniffer_process() {
  tp29_process_rejects();
  tp29_process_frames();
}
