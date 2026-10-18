#pragma once
#include <cstdint>
#ifndef COMELIT_ISR_ATTR
#define COMELIT_ISR_ATTR
#endif

namespace esphome::comelit_simplebus {
enum State : uint8_t { WAIT_CARRIER, WAIT_PREAMBLE, WAIT_BIT_CARRIER, WAIT_BIT_GAP };
enum Kind : uint8_t { NONE, FRAME, REJECT };
struct Record {
  Kind kind{NONE};
  State state{WAIT_CARRIER};
  uint8_t count{0};
  uint32_t bits{0};
  uint32_t dt{0};
};

// Same significant-interval state transitions and timing windows as the working RX.
// Short carrier edges update the timestamp in the caller, before being ignored here.
class Decoder {
 public:
  Record COMELIT_ISR_ATTR feed(uint32_t dt) {
    if (dt < 2000) return {};
    switch (state_) {
      case WAIT_CARRIER:
        if (carrier(dt)) state_ = WAIT_PREAMBLE;
        break;
      case WAIT_PREAMBLE:
        if (preamble(dt)) { count_ = 0; bits_ = 0; state_ = WAIT_BIT_CARRIER; }
        else if (carrier(dt)) state_ = WAIT_PREAMBLE;
        else return reject(dt);
        break;
      case WAIT_BIT_CARRIER:
        if (carrier(dt)) state_ = WAIT_BIT_GAP;
        else if (preamble(dt)) { count_ = 0; bits_ = 0; state_ = WAIT_BIT_CARRIER; }
        else return reject(dt);
        break;
      case WAIT_BIT_GAP:
        if (dt >= 2300 && dt <= 3500) { /* zero */ }
        else if (dt >= 5000 && dt <= 7000) bits_ |= 1UL << count_;
        else return reject(dt);
        if (++count_ == 18) {
          Record r{FRAME, state_, count_, bits_, dt};
          reset();
          return r;
        }
        state_ = WAIT_BIT_CARRIER;
        break;
    }
    return {};
  }
  void COMELIT_ISR_ATTR reset() { state_ = WAIT_CARRIER; count_ = 0; bits_ = 0; }
 private:
  static bool COMELIT_ISR_ATTR carrier(uint32_t dt) { return dt >= 3000 && dt <= 3800; }
  static bool COMELIT_ISR_ATTR preamble(uint32_t dt) { return dt >= 14500 && dt <= 19000; }
  Record COMELIT_ISR_ATTR reject(uint32_t dt) {
    Record r{REJECT, state_, count_, bits_, dt};
    reset();
    return r;
  }
  State state_{WAIT_CARRIER};
  uint8_t count_{0};
  uint32_t bits_{0};
};
struct Frame {
  uint8_t command, address, checksum, calculated;
  bool valid;
};
inline Frame decode(uint32_t bits) {
  uint8_t count = 0;
  for (unsigned i = 0; i < 14; ++i) count += (bits >> i) & 1U;
  uint8_t checksum = (bits >> 14) & 15U;
  return {uint8_t(bits & 63U), uint8_t((bits >> 6) & 255U), checksum,
          uint8_t(count & 15U), checksum == (count & 15U)};
}
}  // namespace esphome::comelit_simplebus
