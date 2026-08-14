#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>

#include "../components/scs_bus/scs_link.h"

using esphome::scs_bus::ScsFrame;
using esphome::scs_bus::ScsLink;

// Deterministic stand-in for the ISR FIFO, TX task, and physical RMT owner.
class Harness final : public ScsLink::Driver {
 public:
  enum class Event { EARLY_ACK, TX_DONE, ADDRESSED_FRAME, COLLISION };

  bool can_transmit() const override { return idle_; }
  uint32_t last_bus_activity_us() const override { return last_activity_us_; }
  bool transmit(const ScsFrame &frame, uint32_t id) override {
    assert(idle_);
    idle_ = false;
    sent_.push_back({frame, id});
    return true;
  }
  void cancel() override { idle_ = true; ++cancels_; }
  void queue(Event event) { events_.push_back(event); }
  bool enqueue(const ScsFrame &frame) {
    if (command_fifo_.size() == 8)
      return false;
    command_fifo_.push_back(frame);
    return true;
  }
  void step(uint32_t now) {
    while (!command_fifo_.empty()) {
      link_.enqueue(command_fifo_.front());
      command_fifo_.pop_front();
    }
    while (!events_.empty()) {
      const Event event = events_.front();
      events_.pop_front();
      if (event == Event::EARLY_ACK)
        link_.on_ack(now + 1);
      else if (event == Event::TX_DONE) {
        idle_ = true;
        last_activity_us_ = now;
        link_.on_transmit_done(sent_.back().id, now);
      } else if (event == Event::ADDRESSED_FRAME)
        link_.note_responder_ack();
      else {
        last_activity_us_ = now;
        link_.on_collision(*this, now);
      }
    }
    link_.run(*this, now);
  }

  struct Sent { ScsFrame frame; uint32_t id; };
  ScsLink link_;
  std::deque<ScsFrame> command_fifo_;
  std::deque<Event> events_;
  std::vector<Sent> sent_;
  bool idle_{true};
  uint32_t last_activity_us_{0};
  uint32_t cancels_{0};
};

int main() {
  const ScsFrame command = ScsFrame::acknowledgment();

  // An ACK timestamped after physical completion remains valid even if RX is
  // dispatched before the RMT completion callback is consumed.
  Harness ordering;
  assert(ordering.enqueue(command));
  ordering.step(100000);
  ordering.step(200000);
  ordering.queue(Harness::Event::EARLY_ACK);
  ordering.queue(Harness::Event::TX_DONE);
  ordering.step(200100);
  assert(!ordering.link_.command_active());

  // An addressed frame preempts normal command work; collision restores the
  // responder ACK only after its OEM randomized access delay.
  Harness preemption;
  assert(preemption.enqueue(command));
  preemption.step(100000);
  preemption.step(200000);
  preemption.queue(Harness::Event::TX_DONE);
  preemption.queue(Harness::Event::ADDRESSED_FRAME);
  preemption.step(200100);
  assert(preemption.sent_.size() == 2 && preemption.sent_[1].frame.is_ack());
  preemption.queue(Harness::Event::COLLISION);
  preemption.step(200101);
  assert(preemption.cancels_ == 1 && preemption.link_.pending_responder_acks() == 1);
  preemption.step(300000);
  assert(preemption.sent_.size() == 3);

  // The command ingress FIFO has deterministic overflow behavior.
  Harness overflow;
  for (uint8_t i = 0; i < 8; ++i)
    assert(overflow.enqueue(command));
  assert(!overflow.enqueue(command));
  overflow.step(100000);
  overflow.step(200000);
  assert(overflow.sent_.size() == 1);
  return 0;
}
