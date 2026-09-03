#include "scsbticino_tx.h"

#include <array>
#include <cassert>
#include <vector>

using namespace esphome::scs_bticino;

namespace {

ScsBticinoData short_frame(uint8_t address = 0xA0) {
  const std::array<uint8_t, 4> payload{0x96, address, 0x6F, 0xA4};
  ScsBticinoData frame;
  assert(ScsBticinoData::from_payload(frame, payload.data(), payload.size()));
  return frame;
}

void start(ScsBticinoTx *tx) { assert(tx->start_next()); }

void append_run(std::vector<ScsBticinoRun> *runs, bool released, uint32_t duration_us) {
  if (!runs->empty() && runs->back().released == released) {
    runs->back().duration_us += duration_us;
  } else {
    runs->push_back({released, duration_us});
  }
}

void test_type_validation() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsBticinoTx extended;
  assert(!extended.enqueue(short_frame(), ScsTxType::EXTENDED));
}

void test_access_collision_rearbitrates() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));
  assert(tx.advance(true, &step, &result));
  assert(tx.state() == ScsTxState::WAIT_ACCESS);
  assert(tx.active());
  assert(!step.drive_dominant);
  assert(step.delay_us >= 150 * SCS_CELL_US);
}

void test_access_delay_uses_oem_seeded_lcg() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));
  // Seed 1 advances to 0x41C67EA6: 150 + 3 * (next >> 23) = 543 cells.
  assert(step.delay_us == 543 * SCS_CELL_US);
}

void test_released_cell_collision_stops_tx() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));  // Random access wait.
  assert(tx.advance(false, &step, &result));  // Start-bit dominant pulse.
  assert(step.drive_dominant);
  assert(tx.advance(false, &step, &result));  // Start-bit released tail.
  assert(!step.drive_dominant);

  // A competing dominant level is only fatal while our scheduled cell is released.
  assert(tx.advance(true, &step, &result));
  assert(tx.active());
  assert(tx.state() == ScsTxState::WAIT_ACCESS);
  assert(!step.drive_dominant);
  assert(step.delay_us >= 150 * SCS_CELL_US);
}

void test_collision_limit_is_256() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));
  for (int collision = 0; collision < 255; collision++) {
    assert(tx.advance(true, &step, &result));
    assert(tx.active());
    assert(tx.state() == ScsTxState::WAIT_ACCESS);
  }
  assert(!tx.advance(true, &step, &result));
  assert(result == ScsTxResult::COLLISION_LIMIT);
  assert(!tx.active());
}

void test_dominant_tx_does_not_require_echo() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
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

void test_every_byte_has_a_start_bit() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));  // Random access wait.
  assert(tx.advance(false, &step, &result));  // First-byte start pulse.
  assert(tx.advance(false, &step, &result));  // First-byte start release.
  while (tx.state() != ScsTxState::STOP)
    assert(tx.advance(false, &step, &result));

  assert(tx.advance(false, &step, &result));
  assert(tx.state() == ScsTxState::INTER_BYTE);
  assert(!step.drive_dominant);
  assert(step.delay_us == SCS_CELL_US + SCS_INTER_BYTE_GAP_US);
  assert(tx.advance(false, &step, &result));
  assert(tx.state() == ScsTxState::START);
  assert(step.drive_dominant);
  assert(step.delay_us == SCS_DOMINANT_US);
}

void test_scheduler_emits_the_codec_frame_runs() {
  const ScsBticinoData frame = short_frame();
  std::vector<ScsBticinoRun> expected;
  assert(ScsBticinoCodec::encode(frame, &expected));

  ScsBticinoTx tx;
  assert(tx.enqueue(frame, ScsTxType::RESPONSE));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));  // Random access wait.
  assert(tx.advance(false, &step, &result));  // First start bit.

  std::vector<ScsBticinoRun> actual;
  append_run(&actual, !step.drive_dominant, step.delay_us);
  while (tx.state() != ScsTxState::END) {
    assert(tx.advance(false, &step, &result));
    append_run(&actual, !step.drive_dominant, step.delay_us);
  }

  assert(actual.size() == expected.size());
  for (size_t index = 0; index < expected.size(); index++) {
    assert(actual[index].released == expected[index].released);
    assert(actual[index].duration_us == expected[index].duration_us);
  }
}

void test_repeat_gap_starts_the_next_frame() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));
  while (tx.state() != ScsTxState::END)
    assert(tx.advance(false, &step, &result));

  assert(tx.advance(false, &step, &result));
  assert(tx.state() == ScsTxState::WAIT_ACCESS);
  assert(!step.drive_dominant);
  assert(step.delay_us == 84 * SCS_CELL_US);
  assert(tx.advance(false, &step, &result));
  assert(tx.state() == ScsTxState::START);
  assert(step.drive_dominant);
}

void test_short_frame_completes_after_three_transmissions() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  int guard = 400;
  while (tx.advance(false, &step, &result) && --guard > 0) {
  }
  assert(guard > 0);
  assert(result == ScsTxResult::SUCCESS);
  assert(!tx.active());
}

void test_cancel_makes_scheduler_available() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(tx.advance(false, &step, &result));
  tx.cancel();
  assert(!tx.active());
  assert(tx.state() == ScsTxState::IDLE);
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
}

void test_queue_reserves_one_slot() {
  ScsBticinoTx tx;
  for (int index = 0; index < 31; index++)
    assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  assert(!tx.enqueue(short_frame(), ScsTxType::SHORT));

  assert(tx.start_next());
  tx.confirm_started();
  tx.cancel();
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
}

void test_busy_bus_defers_queue_start() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::SHORT));
  assert(!tx.ready(true));
  assert(tx.ready(false));
}

void test_queue_preserves_fifo_order_and_failed_start_entry() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(0xA0), ScsTxType::SHORT));
  assert(tx.enqueue(short_frame(0xA1), ScsTxType::SHORT));

  assert(tx.start_next());
  assert(tx.frame().bytes[2] == 0xA0);
  tx.cancel();  // The timer could not arm, so the active entry remains queued.
  assert(tx.start_next());
  assert(tx.frame().bytes[2] == 0xA0);
  tx.confirm_started();
  tx.cancel();

  assert(tx.start_next());
  assert(tx.frame().bytes[2] == 0xA1);
  tx.confirm_started();
  tx.cancel();
  assert(!tx.pending());
}

void test_queue_wraps_without_reordering() {
  ScsBticinoTx tx;
  for (uint8_t address = 0; address < 31; address++)
    assert(tx.enqueue(short_frame(address), ScsTxType::SHORT));

  assert(tx.start_next());
  assert(tx.frame().bytes[2] == 0);
  tx.confirm_started();
  tx.cancel();
  assert(tx.enqueue(short_frame(31), ScsTxType::SHORT));

  for (uint8_t address = 1; address < 32; address++) {
    assert(tx.start_next());
    assert(tx.frame().bytes[2] == address);
    tx.confirm_started();
    tx.cancel();
  }
  assert(!tx.pending());
}

void test_response_timeout() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::RESPONSE));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  int guard = 2000;
  int transmissions = 0;
  while (--guard > 0) {
    const ScsTxState state = tx.state();
    if (!tx.advance(false, &step, &result))
      break;
    if (state == ScsTxState::WAIT_ACCESS && tx.state() == ScsTxState::START && step.drive_dominant)
      transmissions++;
  }
  assert(guard > 0);
  assert(transmissions == 8);
  assert(result == ScsTxResult::RESPONSE_TIMEOUT);
}

void test_received_ack_completes_only_waiting_response() {
  ScsBticinoTx tx;
  assert(tx.enqueue(short_frame(), ScsTxType::RESPONSE));
  start(&tx);
  ScsTxStep step{};
  ScsTxResult result{};
  assert(!tx.complete_response(&result));
  while (tx.state() != ScsTxState::END)
    assert(tx.advance(false, &step, &result));
  assert(tx.advance(false, &step, &result));
  assert(tx.state() == ScsTxState::WAIT_RESPONSE);
  assert(tx.complete_response(&result));
  assert(result == ScsTxResult::SUCCESS);
  assert(!tx.active());
}

void test_local_ack_transmits_once() {
  ScsBticinoTx tx;
  assert(tx.start_ack());
  ScsTxStep step{};
  ScsTxResult result{};
  int transmissions = 0;
  int guard = 100;
  while (--guard > 0) {
    const ScsTxState state = tx.state();
    if (!tx.advance(false, &step, &result))
      break;
    if (state == ScsTxState::WAIT_ACCESS && tx.state() == ScsTxState::START && step.drive_dominant)
      transmissions++;
  }
  assert(guard > 0);
  assert(transmissions == 1);
  assert(result == ScsTxResult::SUCCESS);
  assert(!tx.active());
}

}  // namespace

int main() {
  test_type_validation();
  test_access_collision_rearbitrates();
  test_access_delay_uses_oem_seeded_lcg();
  test_released_cell_collision_stops_tx();
  test_collision_limit_is_256();
  test_dominant_tx_does_not_require_echo();
  test_every_byte_has_a_start_bit();
  test_scheduler_emits_the_codec_frame_runs();
  test_repeat_gap_starts_the_next_frame();
  test_short_frame_completes_after_three_transmissions();
  test_cancel_makes_scheduler_available();
  test_queue_reserves_one_slot();
  test_busy_bus_defers_queue_start();
  test_queue_preserves_fifo_order_and_failed_start_entry();
  test_queue_wraps_without_reordering();
  test_response_timeout();
  test_received_ack_completes_only_waiting_response();
  test_local_ack_transmits_once();
}
