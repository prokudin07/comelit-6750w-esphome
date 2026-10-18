#pragma once
#include <stdint.h>

namespace simplebus {
struct Symbol { uint16_t high_us; uint16_t low_us; };
struct Frame { Symbol symbols[20]; uint8_t checksum; uint32_t bits; };

inline bool encode(uint16_t command, uint16_t address, Frame &out) {
  if (command > 63 || address > 255) return false;
  uint16_t payload = uint16_t(command | (address << 6));
  uint8_t count = 0;
  for (uint16_t p = payload; p; p >>= 1) count += p & 1U;
  out.checksum = count & 15U;
  out.bits = uint32_t(payload) | (uint32_t(out.checksum) << 14);
  out.symbols[0] = {3000, 17000};
  for (unsigned bit = 0; bit < 18; ++bit)
    out.symbols[bit + 1] = {3000, uint16_t((out.bits >> bit) & 1U ? 6000 : 3000)};
  // Both reference implementations send this final burst to delimit the last bit.
  out.symbols[19] = {3000, 1};
  return true;
}
}
