#include <cassert>
#include <cstdint>
#include <vector>

#include "../components/scs_bus/scs_link.h"

using esphome::scs_bus::ScsFrame;
using esphome::scs_bus::ScsLink;

class FakeTransport final : public ScsLink::Driver {
 public:
  bool can_transmit() const override { return idle_; }
  uint32_t last_bus_activity_us() const override { return last_activity_us_; }
  bool transmit(const ScsFrame &frame, uint32_t id) override {
    assert(idle_);
    idle_ = false;
    sent_.push_back({frame, id});
    return true;
  }
  void cancel() override { idle_ = true; cancelled_++; }
  void complete(ScsLink &link, uint32_t at_us) {
    idle_ = true;
    last_activity_us_ = at_us;
    link.on_transmit_done(sent_.back().id, at_us);
  }
  void bus_activity(uint32_t at_us) { last_activity_us_ = at_us; }
  struct Sent { ScsFrame frame; uint32_t id; };
  bool idle_{true};
  uint32_t last_activity_us_{0};
  uint32_t cancelled_{0};
  std::vector<Sent> sent_;
};

int main() {
  FakeTransport transport;
  ScsLink link;
  link.set_random_seed(1);
  const ScsFrame command = ScsFrame::acknowledgment();

  // The M4 randomizes first bus access; it does not transmit immediately.
  assert(link.enqueue(command));
  link.run(transport, 100);
  assert(transport.sent_.empty());
  uint32_t wake_at_us = 0;
  assert(link.next_wakeup_us(&wake_at_us));
  assert(wake_at_us >= 5200 && wake_at_us <= 31720);
  link.run(transport, 100000);
  assert(transport.sent_.size() == 1 && link.attempts() == 1);
  transport.complete(link, 100100);
  assert(link.awaiting_ack());

  // A responder ACK preempts the wait and starts as soon as RX has ended.
  link.note_responder_ack();
  link.run(transport, 100101);
  assert(transport.sent_.size() == 2 && transport.sent_[1].frame.is_ack());
  transport.complete(link, 100200);
  link.on_ack(100300);
  assert(!link.command_active());

  // A responder collision retains the ACK and returns to randomized access.
  link.note_responder_ack();
  link.run(transport, 100400);
  assert(transport.sent_.size() == 3);
  transport.bus_activity(100401);
  link.on_collision(transport, 100401);
  assert(transport.cancelled_ == 1 && link.pending_responder_acks() == 1);
  link.run(transport, 100402);
  assert(transport.sent_.size() == 3);
  link.run(transport, 200000);
  assert(transport.sent_.size() == 4);

  // Collision resets the M4 frame-attempt counter before retrying.
  transport.complete(link, 200100);
  assert(link.enqueue(command));
  link.run(transport, 300000);
  assert(link.attempts() == 0);
  link.run(transport, 400000);
  assert(link.attempts() == 1);
  transport.bus_activity(400001);
  link.on_collision(transport, 400001);
  assert(link.attempts() == 0);
  link.run(transport, 500000);
  assert(link.attempts() == 1);
  return 0;
}
