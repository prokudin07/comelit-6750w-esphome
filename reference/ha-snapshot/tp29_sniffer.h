#pragma once

#include <Arduino.h>
#include "esphome.h"
#include "comelit_control.h"

// ============================================================
// Comelit Simplebus2 decoder + REJECT diagnostics
//
// Actual RX point:
//   TP28 -> 10k -> ESP32-C3 GPIO3
//   Comelit GND -> ESP GND
//
// Frame:
//   6 bits COMMAND
//   8 bits ADDRESS
//   4 bits CHECKSUM
//   LSB first
//
// IMPORTANT:
// Timing windows are intentionally unchanged from working
// decoder. We first measure rejected sequences.
// ============================================================


// ============================================================
// CONFIG
// ============================================================

static const uint8_t RX_PIN = 3;


// ------------------------------------------------------------
// Timing windows
// ------------------------------------------------------------

static const uint32_t CARRIER_MIN_US = 3000;
static const uint32_t CARRIER_MAX_US = 3800;

static const uint32_t PREAMBLE_MIN_US = 14500;
static const uint32_t PREAMBLE_MAX_US = 19000;

static const uint32_t BIT0_MIN_US = 2300;
static const uint32_t BIT0_MAX_US = 3500;

static const uint32_t BIT1_MIN_US = 5000;
static const uint32_t BIT1_MAX_US = 7000;

static const uint32_t MIN_SIGNIFICANT_INTERVAL_US = 2000;


// ============================================================
// DECODER STATE
// ============================================================

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


// ============================================================
// FRAME QUEUE
// ============================================================

static const uint8_t FRAME_QUEUE_SIZE = 8;

struct SimplebusFrame {
  uint8_t bits[18];
};

volatile SimplebusFrame frame_queue[FRAME_QUEUE_SIZE];

volatile uint8_t queue_write = 0;
volatile uint8_t queue_read = 0;

volatile uint32_t good_frames = 0;
volatile uint32_t rejected_sequences = 0;
volatile uint32_t dropped_frames = 0;


// ============================================================
// REJECT DIAGNOSTIC QUEUE
// ============================================================
//
// IMPORTANT:
// No ESP_LOG inside ISR.
// ISR only stores a small diagnostic record here.
// Main loop prints it later.
//
// reason:
//   1 = PREAMBLE
//   2 = BIT_CARRIER
//   3 = BIT_GAP
// ============================================================

static const uint8_t REJECT_QUEUE_SIZE = 16;

struct RejectRecord {
  uint8_t reason;
  uint8_t state;
  uint8_t bit_count;
  uint32_t dt;
  uint8_t bits[18];
};

volatile RejectRecord reject_queue[REJECT_QUEUE_SIZE];

volatile uint8_t reject_write = 0;
volatile uint8_t reject_read = 0;

volatile uint32_t dropped_rejects = 0;


// ============================================================
// STATISTICS
// ============================================================

static uint32_t total_valid_frames = 0;
static uint32_t total_invalid_frames = 0;
static uint32_t apartment1_calls = 0;

static uint32_t command_count[64] = {0};
static uint8_t command_last_address[64] = {0};


// ============================================================
// HELPERS
// ============================================================

inline bool IRAM_ATTR in_range(
    uint32_t value,
    uint32_t min_value,
    uint32_t max_value) {

  return value >= min_value &&
         value <= max_value;
}


// ============================================================
// RESET DECODER
// ============================================================

void IRAM_ATTR reset_decoder() {

  decode_state = WAIT_CARRIER;
  working_count = 0;
}


// ============================================================
// STORE REJECT
// ============================================================

void IRAM_ATTR store_reject(
    uint8_t reason,
    uint32_t dt) {

  rejected_sequences++;

  uint8_t next =
      (reject_write + 1) % REJECT_QUEUE_SIZE;


  // Queue full
  if (next == reject_read) {

    dropped_rejects++;

    return;
  }


  reject_queue[reject_write].reason =
      reason;

  reject_queue[reject_write].state =
      (uint8_t) decode_state;

  reject_queue[reject_write].bit_count =
      working_count;

  reject_queue[reject_write].dt =
      dt;


  // Save already decoded bits
  for (uint8_t i = 0; i < 18; i++) {

    if (i < working_count) {

      reject_queue[reject_write].bits[i] =
          working_bits[i];
    }

    else {

      reject_queue[reject_write].bits[i] = 0;
    }
  }


  reject_write = next;
}


// ============================================================
// STORE COMPLETED FRAME
// ============================================================

void IRAM_ATTR store_frame() {

  uint8_t next =
      (queue_write + 1) % FRAME_QUEUE_SIZE;


  if (next == queue_read) {

    dropped_frames++;

    reset_decoder();

    return;
  }


  for (uint8_t i = 0; i < 18; i++) {

    frame_queue[queue_write].bits[i] =
        working_bits[i];
  }


  queue_write = next;

  good_frames++;

  reset_decoder();
}


// ============================================================
// INTERRUPT
// ============================================================

void IRAM_ATTR tp29_isr() {

  uint32_t now = micros();
  uint32_t previous = last_edge_us;

  last_edge_us = now;


  // First edge
  if (previous == 0) {
    return;
  }


  uint32_t dt = now - previous;


  // Ignore transitions inside 25 kHz carrier
  if (dt < MIN_SIGNIFICANT_INTERVAL_US) {
    return;
  }


  // ==========================================================
  // STATE MACHINE
  // ==========================================================

  switch (decode_state) {


    // --------------------------------------------------------
    // Waiting for ~3.3 ms carrier
    // --------------------------------------------------------

    case WAIT_CARRIER:

      if (in_range(
            dt,
            CARRIER_MIN_US,
            CARRIER_MAX_US)) {

        decode_state = WAIT_PREAMBLE;
      }

      break;


    // --------------------------------------------------------
    // Waiting for ~17 ms preamble gap
    // --------------------------------------------------------

    case WAIT_PREAMBLE:

      if (in_range(
            dt,
            PREAMBLE_MIN_US,
            PREAMBLE_MAX_US)) {

        working_count = 0;

        decode_state = WAIT_BIT_CARRIER;
      }


      // Another carrier-like interval.
      // Continue searching for preamble.
      else if (in_range(
                 dt,
                 CARRIER_MIN_US,
                 CARRIER_MAX_US)) {

        decode_state = WAIT_PREAMBLE;
      }


      else {

        store_reject(
          1,      // PREAMBLE
          dt
        );

        reset_decoder();
      }

      break;


    // --------------------------------------------------------
    // Each bit should begin with ~3.3 ms carrier
    // --------------------------------------------------------

    case WAIT_BIT_CARRIER:

      if (in_range(
            dt,
            CARRIER_MIN_US,
            CARRIER_MAX_US)) {

        decode_state = WAIT_BIT_GAP;
      }


      // If another preamble appears, synchronize to it.
      else if (in_range(
                 dt,
                 PREAMBLE_MIN_US,
                 PREAMBLE_MAX_US)) {

        working_count = 0;

        decode_state = WAIT_BIT_CARRIER;
      }


      else {

        store_reject(
          2,      // BIT_CARRIER
          dt
        );

        reset_decoder();
      }

      break;


    // --------------------------------------------------------
    // Gap determines bit value
    // --------------------------------------------------------

    case WAIT_BIT_GAP: {

      uint8_t bit;


      // ZERO
      if (in_range(
            dt,
            BIT0_MIN_US,
            BIT0_MAX_US)) {

        bit = 0;
      }


      // ONE
      else if (in_range(
                 dt,
                 BIT1_MIN_US,
                 BIT1_MAX_US)) {

        bit = 1;
      }


      // INVALID BIT GAP
      else {

        store_reject(
          3,      // BIT_GAP
          dt
        );

        reset_decoder();

        break;
      }


      // Store bit
      if (working_count < 18) {

        working_bits[working_count] = bit;

        working_count++;
      }


      // Complete frame
      if (working_count == 18) {

        store_frame();
      }

      else {

        decode_state = WAIT_BIT_CARRIER;
      }

      break;
    }
  }
}


// ============================================================
// SETUP
// ============================================================

void tp29_sniffer_setup() {

  pinMode(RX_PIN, INPUT);


  last_edge_us = 0;

  queue_write = 0;
  queue_read = 0;

  reject_write = 0;
  reject_read = 0;

  good_frames = 0;
  rejected_sequences = 0;
  dropped_frames = 0;
  dropped_rejects = 0;

  total_valid_frames = 0;
  total_invalid_frames = 0;
  apartment1_calls = 0;


  for (uint8_t i = 0; i < 64; i++) {

    command_count[i] = 0;
    command_last_address[i] = 0;
  }


  reset_decoder();


  attachInterrupt(
    digitalPinToInterrupt(RX_PIN),
    tp29_isr,
    CHANGE
  );


  ESP_LOGI(
    "simplebus",
    "====================================="
  );

  ESP_LOGI(
    "simplebus",
    "Comelit Simplebus2 diagnostic decoder"
  );

  ESP_LOGI(
    "simplebus",
    "RX: TP28 -> GPIO%d",
    RX_PIN
  );

  ESP_LOGI(
    "simplebus",
    "REJECT diagnostics ENABLED"
  );

  ESP_LOGI(
    "simplebus",
    "====================================="
  );
}


// ============================================================
// PRINT REJECT DIAGNOSTICS
// ============================================================

void tp29_process_rejects() {

  while (true) {

    RejectRecord rec;

    bool have_reject = false;


    noInterrupts();


    if (reject_read != reject_write) {

      rec.reason =
          reject_queue[reject_read].reason;

      rec.state =
          reject_queue[reject_read].state;

      rec.bit_count =
          reject_queue[reject_read].bit_count;

      rec.dt =
          reject_queue[reject_read].dt;


      for (uint8_t i = 0; i < 18; i++) {

        rec.bits[i] =
            reject_queue[reject_read].bits[i];
      }


      reject_read =
          (reject_read + 1) %
          REJECT_QUEUE_SIZE;


      have_reject = true;
    }


    interrupts();


    if (!have_reject) {
      break;
    }


    // --------------------------------------------------------
    // Reason text
    // --------------------------------------------------------

    const char *reason_text = "UNKNOWN";


    if (rec.reason == 1) {

      reason_text = "PREAMBLE";
    }

    else if (rec.reason == 2) {

      reason_text = "BIT_CARRIER";
    }

    else if (rec.reason == 3) {

      reason_text = "BIT_GAP";
    }


    // --------------------------------------------------------
    // State text
    // --------------------------------------------------------

    const char *state_text = "UNKNOWN";


    if (rec.state == WAIT_CARRIER) {

      state_text = "WAIT_CARRIER";
    }

    else if (rec.state == WAIT_PREAMBLE) {

      state_text = "WAIT_PREAMBLE";
    }

    else if (rec.state == WAIT_BIT_CARRIER) {

      state_text = "WAIT_BIT_CARRIER";
    }

    else if (rec.state == WAIT_BIT_GAP) {

      state_text = "WAIT_BIT_GAP";
    }


    // --------------------------------------------------------
    // Build partial bit string
    // --------------------------------------------------------

    char partial[20];

    uint8_t n = rec.bit_count;


    if (n > 18) {
      n = 18;
    }


    for (uint8_t i = 0; i < n; i++) {

      partial[i] =
          rec.bits[i] ? '1' : '0';
    }


    partial[n] = '\0';


    // --------------------------------------------------------
    // Print
    // --------------------------------------------------------

    ESP_LOGW(
      "simplebus",
      ""
    );


    ESP_LOGW(
      "simplebus",
      "!!!!!!!!!! REJECT !!!!!!!!!!!"
    );


    ESP_LOGW(
      "simplebus",
      "REASON    : %s",
      reason_text
    );


    ESP_LOGW(
      "simplebus",
      "STATE     : %s",
      state_text
    );


    ESP_LOGW(
      "simplebus",
      "BITS      : %u / 18",
      rec.bit_count
    );


    ESP_LOGW(
      "simplebus",
      "PARTIAL   : %s",
      rec.bit_count > 0
        ? partial
        : "(none)"
    );


    ESP_LOGW(
      "simplebus",
      "BAD dt    : %lu us",
      (unsigned long) rec.dt
    );


    ESP_LOGW(
      "simplebus",
      "REJECTS   : %lu",
      (unsigned long) rejected_sequences
    );


    ESP_LOGW(
      "simplebus",
      "DROPPED R : %lu",
      (unsigned long) dropped_rejects
    );


    ESP_LOGW(
      "simplebus",
      "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    );
  }
}


// ============================================================
// PROCESS VALID FRAMES
// ============================================================

void tp29_process_frames() {

  while (true) {

    uint8_t bits[18];

    bool have_frame = false;


    // --------------------------------------------------------
    // Get frame
    // --------------------------------------------------------

    noInterrupts();


    if (queue_read != queue_write) {

      for (uint8_t i = 0; i < 18; i++) {

        bits[i] =
            frame_queue[queue_read].bits[i];
      }


      queue_read =
          (queue_read + 1) %
          FRAME_QUEUE_SIZE;


      have_frame = true;
    }


    interrupts();


    if (!have_frame) {
      break;
    }


    // ========================================================
    // DECODE
    // ========================================================

    uint8_t command = 0;
    uint8_t address = 0;
    uint8_t checksum = 0;


    // Command 0..5
    for (uint8_t i = 0; i < 6; i++) {

      if (bits[i]) {

        command |=
            (1U << i);
      }
    }


    // Address 6..13
    for (uint8_t i = 0; i < 8; i++) {

      if (bits[6 + i]) {

        address |=
            (1U << i);
      }
    }


    // Checksum 14..17
    for (uint8_t i = 0; i < 4; i++) {

      if (bits[14 + i]) {

        checksum |=
            (1U << i);
      }
    }


    // ========================================================
    // CHECKSUM
    // ========================================================

    uint8_t calculated_checksum = 0;


    for (uint8_t i = 0; i < 14; i++) {

      calculated_checksum += bits[i];
    }


    calculated_checksum &= 0x0F;


    bool valid =
        (checksum == calculated_checksum);

    // Publish only from the main loop, after checksum verification.
    comelit::on_frame(command, address, valid);


    // ========================================================
    // STATISTICS
    // ========================================================

    if (valid) {

      total_valid_frames++;


      command_count[command]++;

      command_last_address[command] =
          address;
    }

    else {

      total_invalid_frames++;
    }


    // ========================================================
    // OUR CALL
    // ========================================================

    bool our_call =
        valid &&
        command == 50 &&
        address == comelit::apartment_address;


    if (our_call) {

      apartment1_calls++;


      ESP_LOGI(
        "simplebus",
        ""
      );


      ESP_LOGI(
        "simplebus",
        "*************************************"
      );


      ESP_LOGI(
        "simplebus",
        "*** CALL TO APARTMENT %u !!! ***",
        address
      );


      ESP_LOGI(
        "simplebus",
        "*** CALL COUNT: %lu",
        (unsigned long) apartment1_calls
      );


      ESP_LOGI(
        "simplebus",
        "*************************************"
      );
    }


    // ========================================================
    // RAW STRING
    // ========================================================

    char raw[32];

    char command_bits[7];
    char address_bits[9];
    char checksum_bits[5];


    for (uint8_t i = 0; i < 6; i++) {

      command_bits[i] =
          bits[i] ? '1' : '0';
    }

    command_bits[6] = '\0';


    for (uint8_t i = 0; i < 8; i++) {

      address_bits[i] =
          bits[6 + i] ? '1' : '0';
    }

    address_bits[8] = '\0';


    for (uint8_t i = 0; i < 4; i++) {

      checksum_bits[i] =
          bits[14 + i] ? '1' : '0';
    }

    checksum_bits[4] = '\0';


    snprintf(
      raw,
      sizeof(raw),
      "%s %s %s",
      command_bits,
      address_bits,
      checksum_bits
    );


    // ========================================================
    // FRAME LOG
    // ========================================================

    ESP_LOGI(
      "simplebus",
      ""
    );


    ESP_LOGI(
      "simplebus",
      "========== SIMPLEBUS FRAME =========="
    );


    ESP_LOGI(
      "simplebus",
      "RAW LSB   : %s",
      raw
    );


    ESP_LOGI(
      "simplebus",
      "COMMAND   : %u (0x%02X)",
      command,
      command
    );


    ESP_LOGI(
      "simplebus",
      "ADDRESS   : %u (0x%02X)",
      address,
      address
    );


    ESP_LOGI(
      "simplebus",
      "CHECKSUM  : %u",
      checksum
    );


    ESP_LOGI(
      "simplebus",
      "CALC CS   : %u",
      calculated_checksum
    );


    ESP_LOGI(
      "simplebus",
      "VALID     : %s",
      valid ? "YES" : "NO"
    );


    ESP_LOGI(
      "simplebus",
      "TOTAL     : valid=%lu invalid=%lu calls=%lu",
      (unsigned long) total_valid_frames,
      (unsigned long) total_invalid_frames,
      (unsigned long) apartment1_calls
    );


    ESP_LOGI(
      "simplebus",
      "ISR       : frames=%lu rejected=%lu dropped=%lu",
      (unsigned long) good_frames,
      (unsigned long) rejected_sequences,
      (unsigned long) dropped_frames
    );


    ESP_LOGI(
      "simplebus",
      "====================================="
    );


    // ========================================================
    // COMMAND STATISTICS
    // ========================================================

    ESP_LOGI(
      "simplebus",
      "COMMAND STATISTICS:"
    );


    bool any_command = false;


    for (uint8_t cmd = 0; cmd < 64; cmd++) {

      if (command_count[cmd] == 0) {
        continue;
      }


      any_command = true;


      ESP_LOGI(
        "simplebus",
        "CMD %2u (0x%02X): %lu times, last addr=%u",
        cmd,
        cmd,
        (unsigned long) command_count[cmd],
        command_last_address[cmd]
      );
    }


    if (!any_command) {

      ESP_LOGI(
        "simplebus",
        "No valid commands yet"
      );
    }


    ESP_LOGI(
      "simplebus",
      "-------------------------------------"
    );
  }
}


// ============================================================
// MAIN PROCESS
// ============================================================

void tp29_sniffer_process() {

  // First dump diagnostic failures
  tp29_process_rejects();

  // Then normal decoded frames
  tp29_process_frames();

  // Short atomic snapshot. The original RX interrupt remains attached.
  noInterrupts();
  const uint32_t rx_edge = last_edge_us;
  interrupts();
  comelit::process(rx_edge, digitalRead(RX_PIN) == HIGH);
}