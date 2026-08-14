#include <cassert>
#include <vector>

#include "../components/scs_bus/scs_link.h"

using esphome::scs_bus::ScsLink;
using esphome::scs_bus::ScsTelegram;

class FakeTransport final : public ScsLink::Driver {
 public:
  bool can_transmit() const override { return idle_; }
  uint32_t last_bus_activity_us() const override { return last_activity_us_; }
  bool transmit(const ScsTelegram &telegram, uint32_t id) override {
    idle_ = false;
    sent_.push_back({telegram, id});
    return true;
  }
  void cancel() override { idle_ = true; }
  struct Sent { ScsTelegram telegram; uint32_t id; };
  bool idle_{true};
  uint32_t last_activity_us_{0};
  std::vector<Sent> sent_;
};

int main() {
  FakeTransport transport;
  ScsLink link;
  link.set_random_seed(1);
  assert(link.enqueue(ScsTelegram::acknowledgment()));
  link.run(transport, 100000);
  link.run(transport, 200000);
  assert(transport.sent_.size() == 1);
  link.on_transmit_done(transport.sent_.front().id, 100100);
  assert(link.awaiting_ack());
  link.on_ack(100200);
  assert(!link.command_active());
  return 0;
}
