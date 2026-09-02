#include "scsbticino_tx.h"

#include <array>
#include <cassert>

using namespace esphome::scs_bticino;

namespace {

ScsBticinoData short_frame() {
  const std::array<uint8_t, 4> payload{0x96, 0xA0, 0x6F, 0xA4};
  ScsBticinoData frame;
  assert(ScsBticinoData::from_payload(frame, payload.data(), payload.size()));
  return frame;
}

void test_type_validation() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  ScsBticinoTx extended;
  assert(!extended.enqueue(short_frame(), ScsTxType::EXTENDED));
}

void test_access_collision_rearbitrates() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));
  assert(step.check_released);
  assert(tx.advance(true, &step, &result));
  assert(tx.state() == ScsTxState::IDLE);
  assert(tx.active());
}

void test_released_cell_collision_stops_tx() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));  // Random access wait.
  assert(tx.advance(false, &step, &result));  // Start-bit dominant pulse.
  assert(step.drive_dominant);
  assert(tx.advance(false, &step, &result));  // Start-bit released tail.
  assert(!step.drive_dominant);
  assert(step.check_released);

  // A competing dominant level is only fatal while our scheduled cell is released.
  assert(tx.advance(true, &step, &result));
  assert(tx.active());
  assert(tx.state() == ScsTxState::IDLE);
}

void test_dominant_tx_does_not_require_echo() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));
  assert(tx.advance(false, &step, &result));
  assert(step.drive_dominant);
  // The OEM only checks released checkpoints. Missing dominant readback is not
  // a collision, so the start pulse advances normally with RX released.
  assert(tx.advance(false, &step, &result));
  assert(tx.state() == ScsTxState::BYTE);
}

void test_response_timeout() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::RESPONSE));
  ScsTxStep step{};
  ScsTxResult result{};
  int guard = 100;
  while (tx.advance(false, &step, &result) && --guard > 0) {
  }
  assert(guard > 0);
  assert(result == ScsTxResult::RESPONSE_TIMEOUT);
}

}  // namespace

int main() {
  test_type_validation();
  test_access_collision_rearbitrates();
  test_released_cell_collision_stops_tx();
  test_dominant_tx_does_not_require_echo();
  test_response_timeout();
}
