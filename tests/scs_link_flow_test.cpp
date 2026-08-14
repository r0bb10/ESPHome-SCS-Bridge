#include <cassert>

#include "../components/scs_bus/scs_link.h"

using esphome::scs_bus::ScsLink;
using esphome::scs_bus::ScsTelegram;

class FakeTransport final : public ScsLink::Driver {
 public:
  bool can_transmit() const override { return idle_; }
  uint32_t last_bus_activity_us() const override { return 0; }
  bool transmit(const ScsTelegram &, uint32_t id) override { idle_ = false; id_ = id; return true; }
  void cancel() override { idle_ = true; cancelled_ = true; }
  bool idle_{true};
  bool cancelled_{false};
  uint32_t id_{0};
};

int main() {
  FakeTransport transport;
  ScsLink link;
  link.set_random_seed(1);
  assert(link.enqueue(ScsTelegram::acknowledgment()));
  link.run(transport, 100000);
  link.run(transport, 200000);
  assert(transport.id_ != 0);
  link.on_collision(transport, 100001);
  assert(transport.cancelled_ && link.attempts() == 0);
  return 0;
}
