#!/usr/bin/env python3
"""Build/run real decoder regression checks; use only the standard library."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
log = (ROOT / 'docs/episode_addr3_log.md').read_text()
raw_frames = re.findall(r'RAW LSB\s*:\s*([01]{6}) ([01]{8}) ([01]{4})', log)
assert len(raw_frames) == 9, 'Unexpected fixture contents'
fixtures = []
for command, address, checksum in raw_frames:
    bits = command + address + checksum
    value = sum(int(v) << i for i, v in enumerate(bits))
    fixtures.append(f'check_frame(0x{value:x}, {int(command[::-1], 2)}, {int(address[::-1], 2)}, {int(checksum[::-1], 2)});')
source = r'''
#include "decoder.h"
#include <cassert>
#include <iostream>
using namespace esphome::comelit_simplebus;
uint32_t encoded(unsigned command, unsigned address) {
  uint32_t bits = command | (address << 6);
  unsigned count = 0;
  for (unsigned i=0; i<14; ++i) count += (bits >> i) & 1U;
  return bits | (count << 14);
}
Record feed_packet(Decoder &decoder, uint32_t bits, uint32_t burst=3300, uint32_t preamble=16700) {
  decoder.feed(burst);
  decoder.feed(preamble);
  Record result;
  for (unsigned i=0; i<18; ++i) {
    decoder.feed(burst);
    result = decoder.feed((bits >> i) & 1 ? 5900 : 2800);
  }
  return result;
}
void check_frame(uint32_t bits, unsigned command, unsigned address, unsigned checksum) {
  auto f = decode(bits);
  assert(f.valid && f.command == command && f.address == address && f.checksum == checksum);
  Decoder decoder;
  auto r = feed_packet(decoder, bits);
  assert(r.kind == FRAME && r.bits == bits && r.count == 18);
}
int main() {
  FIXTURES
  check_frame(encoded(50,1),50,1,4);
  for (unsigned cmd: {16,17,18,22,23,48,49,50,63}) {
    for (unsigned address: {0,1,3,84,92,111,255}) {
      auto bits = encoded(cmd,address);
      check_frame(bits,cmd,address,decode(bits).checksum);
    }
  }
  Decoder d;
  for (unsigned cmd: {48,48,50}) {
    auto r=feed_packet(d,encoded(cmd,1));
    assert(r.kind==FRAME && decode(r.bits).command==cmd);
  }
  // A checksum mismatch must survive timing decoding and fail checksum validation.
  auto corrupted=encoded(50,1) ^ (1U<<14);
  auto bad=feed_packet(d,corrupted);
  assert(bad.kind==FRAME && !decode(bad.bits).valid);
  // Malformed preamble, then recovery for a good frame.
  d.feed(3300);
  auto r=d.feed(14000);
  assert(r.kind==REJECT && r.state==WAIT_PREAMBLE && r.count==0 && r.dt==14000);
  assert(feed_packet(d,encoded(50,1)).kind==FRAME);
  // Rejection after a partial payload retains its bits and state.
  d.feed(3300);d.feed(16700);d.feed(3300);d.feed(5900);d.feed(3300);
  r=d.feed(4500);
  assert(r.kind==REJECT && r.state==WAIT_BIT_GAP && r.count==1 && r.bits==1);
  assert(feed_packet(d,encoded(17,3)).kind==FRAME);
  // The inherited preamble resynchronization branch.
  d.feed(3300);d.feed(16700);d.feed(16700);
  d.feed(3300);r=d.feed(2800);
  assert(r.kind==NONE);
  d.reset();
  // Short adjacent-edge intervals are ignored in every decoder phase.
  d.feed(3300);d.feed(20);d.feed(16700);d.feed(20);
  for(unsigned i=0;i<18;++i){d.feed(3300);d.feed(20);r=d.feed(2800);}
  assert(r.kind==FRAME && r.bits==0);
  for(uint32_t burst: {3000U,3800U}) for(uint32_t pre: {14500U,19000U}) {
    d.reset();assert(feed_packet(d,encoded(48,1),burst,pre).kind==FRAME);
  }
  for(uint32_t gap: {2300U,3500U,5000U,7000U}) {
    d.reset();d.feed(3300);d.feed(16700);d.feed(3300);
    assert(d.feed(gap).kind==NONE);d.feed(3300);r=d.feed(4500);
    assert(r.kind==REJECT && r.bits==(gap>=5000 ? 1U:0U));
  }
  // Unsigned edge timestamp subtraction, as used by the ISR, across wrap.
  uint32_t previous=0xFFFFF000U, now=previous+16700U;
  assert(uint32_t(now-previous)==16700U);
  d.reset();d.feed(3300);d.feed(now-previous);
  d.feed(3300);r=d.feed(4500);
  assert(r.kind==REJECT && r.state==WAIT_BIT_GAP);
  std::cout << "PASS: log frames, known commands, synthetic 48/48/50, checksum rejection, timing bounds, recovery and timestamp rollover\n";
}
'''.replace('FIXTURES', '\n  '.join(fixtures))
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    cpp = directory / 'decoder_test.cpp'
    cpp.write_text(source)
    executable = directory / 'decoder_test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I', str(ROOT/'components/comelit_simplebus'), str(cpp), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print(f'Validated {len(raw_frames)} original logged frame fixtures.')
