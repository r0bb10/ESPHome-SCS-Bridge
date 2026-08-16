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
  {
    FakeTransport transport;
    ScsLink link;
    link.set_random_seed(1);
    assert(link.enqueue(ScsTelegram::acknowledgment()));
    link.run(transport, 100000);
    link.run(transport, 200000);
    assert(transport.id_ != 0);
    link.on_collision(transport, 100001);
    assert(transport.cancelled_ && link.attempts() == 0);
  }

  {
    FakeTransport transport;
    ScsLink link;
    link.set_random_seed(1);
    assert(link.enqueue(ScsTelegram::acknowledgment(), ScsLink::Mode::NO_ACK));
    for (uint8_t attempt = 0; attempt < 8; attempt++) {
      link.run(transport, 100000 + attempt * 100000);
      link.run(transport, 200000 + attempt * 100000);
      assert(link.attempts() == attempt + 1);
      link.on_transmit_done(transport.id_, 200100 + attempt * 100000);
      transport.idle_ = true;
    }
    assert(!link.command_active());
  }

  {
    FakeTransport transport;
    ScsLink link;
    link.set_random_seed(1);
    assert(link.enqueue(ScsTelegram::acknowledgment()));
    link.run(transport, 100000);
    link.run(transport, 200000);
    for (uint8_t attempt = 1; attempt <= 3; attempt++) {
      link.on_transmit_done(transport.id_, 200100 + attempt * 10000);
      transport.idle_ = true;
      link.run(transport, 203000 + attempt * 10000);  // ACK timeout
      if (attempt < 3)
        link.run(transport, 210000 + attempt * 10000);  // retry delay
    }
    assert(!link.command_active());
  }
  return 0;
}
