#include "scsbticino_codec.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using esphome::scs_bticino::ScsBticinoCodec;
using esphome::scs_bticino::ScsBticinoData;
using esphome::scs_bticino::ScsBticinoRun;

namespace {

std::vector<ScsBticinoRun> encode(const std::initializer_list<uint8_t> &bytes) {
  ScsBticinoData data;
  assert(ScsBticinoData::from_bytes(data, bytes.begin(), bytes.size()));
  std::vector<ScsBticinoRun> runs;
  assert(ScsBticinoCodec::encode(data, &runs));
  return runs;
}

void append_run(std::vector<ScsBticinoRun> *runs, bool released, uint32_t duration_us) {
  if (!runs->empty() && runs->back().released == released) {
    runs->back().duration_us += duration_us;
  } else {
    runs->push_back({released, duration_us});
  }
}

std::vector<ScsBticinoRun> encode_raw(const std::initializer_list<uint8_t> &bytes) {
  std::vector<ScsBticinoRun> runs;
  for (auto byte : bytes) {
    append_run(&runs, false, 35);
    append_run(&runs, true, 69);
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (byte & (1U << bit)) {
        append_run(&runs, true, 104);
      } else {
        append_run(&runs, false, 35);
        append_run(&runs, true, 69);
      }
    }
    append_run(&runs, true, 174);
  }
  return runs;
}

void expect_decode(const std::initializer_list<uint8_t> &bytes) {
  const auto runs = encode(bytes);
  ScsBticinoData decoded;
  assert(ScsBticinoCodec::decode(runs, &decoded));
  assert(decoded.length == bytes.size());
  for (size_t index = 0; index < bytes.size(); index++)
    assert(decoded.bytes[index] == *(bytes.begin() + index));
}

void test_standard_frame() {
  expect_decode({0xA8, 0x96, 0xA0, 0x6F, 0xA4, 0xFD, 0xA3});
}

void test_rx_ignores_final_byte_but_tx_does_not() {
  ScsBticinoData data;
  const std::array<uint8_t, 7> bytes{0xA8, 0x96, 0xA0, 0x6F, 0xA4, 0xFD, 0x00};
  assert(ScsBticinoData::from_bytes(data, bytes.data(), bytes.size()));
  assert(!data.is_transmittable());
  std::vector<ScsBticinoRun> runs;
  assert(!ScsBticinoCodec::encode(data, &runs));

  const auto received_runs = encode_raw({0xA8, 0x96, 0xA0, 0x6F, 0xA4, 0xFD, 0x00});
  ScsBticinoData decoded;
  assert(ScsBticinoCodec::decode(received_runs, &decoded));
  assert(decoded.bytes[6] == 0x00);
}

void test_extended_frame_is_never_truncated() {
  const std::array<uint8_t, 8> payload{0xD1, 0x11, 0x22, 0x33, 0xD1, 0xA3, 0x44, 0x55};
  ScsBticinoData data;
  assert(ScsBticinoData::from_payload(data, payload.data(), payload.size()));
  assert(data.bytes[5] == 0xD1);  // Its first seven bytes are a valid standard frame.
  std::vector<ScsBticinoRun> runs;
  assert(ScsBticinoCodec::encode(data, &runs));
  ScsBticinoData decoded;
  assert(ScsBticinoCodec::decode(runs, &decoded));
  assert(decoded.length == 11);
  assert(decoded.bytes == data.bytes);
}

void test_ack() {
  const auto runs = encode({0xA5});
  ScsBticinoData decoded;
  assert(ScsBticinoCodec::decode(runs, &decoded));
  assert(decoded.is_ack());
}

void test_bad_checksum_is_rejected() {
  const auto corrupt = encode_raw({0xA8, 0x96, 0xA0, 0x6F, 0xA4, 0xFC, 0xA3});
  ScsBticinoData decoded;
  assert(!ScsBticinoCodec::decode(corrupt, &decoded));
}

void test_stretched_dominant_is_rejected() {
  auto runs = encode({0xA8, 0x96, 0xA0, 0x6F, 0xA4, 0xFD, 0xA3});
  runs.front().duration_us = 56;
  ScsBticinoData decoded;
  assert(!ScsBticinoCodec::decode(runs, &decoded));
}

void test_recovers_after_corrupt_candidate() {
  std::vector<ScsBticinoRun> runs{{false, 20}, {true, 100}};
  const auto valid = encode({0xA8, 0x96, 0xA0, 0x6F, 0xA4, 0xFD, 0xA3});
  runs.insert(runs.end(), valid.begin(), valid.end());

  ScsBticinoData decoded;
  assert(ScsBticinoCodec::decode(runs, &decoded));
  assert(decoded.length == 7);
  assert(decoded.bytes[0] == 0xA8);
}

}  // namespace

int main() {
  test_standard_frame();
  test_rx_ignores_final_byte_but_tx_does_not();
  test_extended_frame_is_never_truncated();
  test_ack();
  test_bad_checksum_is_rejected();
  test_stretched_dominant_is_rejected();
  test_recovers_after_corrupt_candidate();
}
