#include "scsbticino_rx_policy.h"
#include "scsbticino_tx.h"

#include <array>
#include <cassert>
#include <vector>

using namespace esphome::scs_bticino;

namespace {

ScsBticinoData short_frame() {
  const std::array<uint8_t, 4> payload{0x96, 0xA0, 0x6F, 0xA4};
  ScsBticinoData frame;
  assert(ScsBticinoData::from_payload(frame, payload.data(), payload.size()));
  return frame;
}

ScsBticinoData locally_addressed_frame() {
  const std::array<uint8_t, 4> payload{0x42, 0x00, 0x10, 0x00};
  ScsBticinoData frame;
  assert(ScsBticinoData::from_payload(frame, payload.data(), payload.size()));
  return frame;
}

struct FakeBusTimer {
  bool busy{false};
  bool dominant{false};
  bool timer_armed{false};
  uint32_t delay_us{0};
  std::vector<bool> driven_levels;

  void drive(bool dominant_level) { this->driven_levels.push_back(dominant_level); }
  void arm(uint32_t delay) {
    this->timer_armed = true;
    this->delay_us = delay;
  }
  void stop() { this->timer_armed = false; }
};

// Test-only orchestration of the same pure scheduler and RX policy used by
// the component. It deliberately does not enter the ESP-IDF ISR path.
class ControllerHarness {
 public:
  explicit ControllerHarness(ScsBticinoRxPolicy policy) : policy_(policy) {}

  bool send(const ScsBticinoData &frame, ScsTxType type) { return this->tx_.enqueue(frame, type); }

  bool start() {
    if (this->tx_.active() || this->bus.busy || (!this->pending_ack_ && !this->tx_.pending()))
      return false;
    const bool local_ack = this->pending_ack_;
    assert(local_ack ? this->tx_.start_ack() : this->tx_.start_next());
    ScsTxStep step{};
    ScsTxResult ignored{};
    assert(this->tx_.advance(false, &step, &ignored));
    this->bus.drive(step.drive_dominant);
    this->bus.arm(step.delay_us);
    if (local_ack)
      this->pending_ack_ = false;
    else
      this->tx_.confirm_started();
    return true;
  }

  void tick(bool access_contended = false) {
    assert(this->bus.timer_armed);
    ScsTxStep step{};
    ScsTxResult result{};
    const bool rx_dominant = this->tx_.awaiting_access() ? access_contended : this->bus.dominant;
    const bool active = this->tx_.advance(rx_dominant, &step, &result);
    if (!active) {
      this->bus.drive(false);
      this->bus.stop();
      this->result_ = result;
      return;
    }
    this->bus.drive(step.drive_dominant);
    this->bus.arm(step.delay_us);
  }

  void receive(const ScsBticinoData &frame) {
    const auto action = this->policy_.action_for(frame, this->tx_.state() == ScsTxState::WAIT_RESPONSE);
    if (action == ScsBticinoRxAction::COMPLETE_RESPONSE) {
      assert(this->tx_.complete_response(&this->result_));
      this->bus.stop();
    } else if (action == ScsBticinoRxAction::QUEUE_LOCAL_ACK) {
      this->pending_ack_ = true;
    }
  }

  FakeBusTimer bus;
  ScsBticinoTx tx_;
  ScsTxResult result_{ScsTxResult::SUCCESS};

 private:
  ScsBticinoRxPolicy policy_;
  bool pending_ack_{false};
};

void test_local_ack_precedes_queued_frame() {
  ControllerHarness controller(ScsBticinoRxPolicy(ScsBticinoIdentity{0, 0x42, 1}));
  assert(controller.send(short_frame(), ScsTxType::SHORT));
  controller.receive(locally_addressed_frame());
  assert(controller.start());
  assert(controller.tx_.local_ack());
  assert(controller.tx_.pending());

  while (controller.bus.timer_armed)
    controller.tick();
  assert(controller.result_ == ScsTxResult::SUCCESS);
  assert(controller.start());
  assert(!controller.tx_.local_ack());
  assert(controller.tx_.frame().bytes == short_frame().bytes);
}

void test_inbound_ack_stops_type_zero_timer() {
  ControllerHarness controller(ScsBticinoRxPolicy{});
  assert(controller.send(short_frame(), ScsTxType::RESPONSE));
  assert(controller.start());
  while (controller.tx_.state() != ScsTxState::WAIT_RESPONSE)
    controller.tick();

  controller.receive(ScsBticinoData::acknowledgment());
  assert(!controller.bus.timer_armed);
  assert(!controller.tx_.active());
  assert(controller.result_ == ScsTxResult::SUCCESS);
}

void test_collision_releases_bus_before_rearbitration() {
  ControllerHarness controller(ScsBticinoRxPolicy{});
  assert(controller.send(short_frame(), ScsTxType::SHORT));
  assert(controller.start());
  controller.tick();  // Access wait to start pulse.
  controller.tick();  // Start pulse to released tail.
  controller.bus.dominant = true;
  controller.tick();

  assert(controller.tx_.state() == ScsTxState::WAIT_ACCESS);
  assert(!controller.bus.driven_levels.back());
  assert(controller.bus.delay_us >= 150 * SCS_CELL_US);
}

}  // namespace

int main() {
  test_local_ack_precedes_queued_frame();
  test_inbound_ack_stops_type_zero_timer();
  test_collision_releases_bus_before_rearbitration();
}
