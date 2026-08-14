#include "scs_link.h"

#include <vector>

namespace esphome::scs_bus {

namespace {

class ScsOemTransaction {
 public:
  static constexpr uint8_t SINGLE = 0;
  static constexpr uint8_t NO_ACK = 1;
  static constexpr uint8_t ACK = 2;

  void begin(uint32_t id, uint8_t mode, uint8_t attempts = 0) {
    id_ = id;
    mode_ = mode;
    attempts_ = attempts;
    awaiting_ack_ = false;
    complete_at_us_ = 0;
  }
  void clear() { id_ = 0; }
  uint8_t attempts() const { return attempts_; }
  bool awaiting_ack() const { return awaiting_ack_; }
  uint32_t complete_at_us() const { return complete_at_us_; }
  void start_attempt() { attempts_++; }
  bool exhausted() const { return attempts_ >= maximum_attempts(mode_); }
  bool complete(uint32_t id, uint32_t complete_at_us) {
    if (id != id_)
      return false;
    complete_at_us_ = complete_at_us;
    awaiting_ack_ = mode_ == ACK;
    return true;
  }
  bool accept_ack(uint32_t received_at_us, uint32_t timeout_us) const {
    return awaiting_ack_ && static_cast<int32_t>(received_at_us - complete_at_us_) >= 0 &&
           static_cast<int32_t>(received_at_us - (complete_at_us_ + timeout_us)) <= 0;
  }
  void collided() { awaiting_ack_ = false; }
  void reset_attempts() { attempts_ = 0; }

 private:
  static constexpr uint8_t maximum_attempts(uint8_t mode) { return mode == NO_ACK ? 8 : mode == ACK ? 3 : 1; }

  uint32_t id_{0};
  uint32_t complete_at_us_{0};
  uint8_t attempts_{0};
  uint8_t mode_{SINGLE};
  bool awaiting_ack_{false};
};

constexpr size_t MAX_COMMANDS = 8;
constexpr uint32_t ACK_WAIT_US = 2843;
constexpr uint32_t ACK_RETRY_DELAY_US = 2912;
constexpr uint32_t OEM_TICK_NS = 34670;
constexpr uint32_t NO_EARLY_ACK = UINT32_MAX;

bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

}  // namespace

struct ScsLink::Impl {
  struct Command {
    ScsFrame frame{};
    Mode mode{Mode::ACK};
    uint32_t access_delay_us{0};
    uint32_t next_attempt_us{0};
    ScsOemTransaction transaction{};
  };
  enum class Wire : uint8_t { NONE, COMMAND, RESPONDER };

  uint32_t access_deadline(uint32_t scheduled_at_us, uint32_t delay_us) const {
    const uint32_t quiet_at_us = last_bus_activity_us + delay_us;
    return reached(scheduled_at_us, quiet_at_us) ? scheduled_at_us : quiet_at_us;
  }
  bool access_ready(uint32_t now_us, uint32_t scheduled_at_us, uint32_t delay_us) const {
    return reached(now_us, access_deadline(scheduled_at_us, delay_us));
  }
  uint32_t random_access_delay() {
    random_state = (random_state * 1103515245U + 12345U) & 0x7FFFFFFFU;
    const uint32_t ticks = ((random_state >> 23U) * 3U) + 0x96U;
    return (ticks * OEM_TICK_NS) / 1000U;
  }
  void schedule_command(uint32_t now_us, bool collision = false) {
    if (collision || command.transaction.attempts() == 0 || command.mode == Mode::NO_ACK)
      command.access_delay_us = random_access_delay();
    else
      command.access_delay_us = ACK_RETRY_DELAY_US;
    command.next_attempt_us = now_us + command.access_delay_us;
  }
  void finish_command() {
    command_active = false;
    command.transaction.clear();
  }
  void start(Driver &driver, const ScsFrame &frame, Wire next_wire, uint32_t now_us) {
    wire_transaction_id = next_transaction_id++;
    if (next_wire == Wire::COMMAND)
      command.transaction.begin(wire_transaction_id, static_cast<uint8_t>(command.mode), command.transaction.attempts());
    wire = next_wire;
    if (!driver.transmit(frame, wire_transaction_id)) {
      wire = Wire::NONE;
      if (next_wire == Wire::RESPONDER)
        responder_acks++;
      else if (command.transaction.exhausted())
        finish_command();
      else
        schedule_command(now_us);
    }
  }

  std::vector<Command> commands;
  Command command{};
  uint32_t next_transaction_id{1};
  uint32_t wire_transaction_id{0};
  uint32_t responder_acks{0};
  uint32_t early_ack_at_us{NO_EARLY_ACK};
  uint32_t random_state{1};
  uint32_t last_bus_activity_us{0};
  uint32_t responder_access_delay_us{0};
  uint32_t responder_next_attempt_us{0};
  Wire wire{Wire::NONE};
  bool command_active{false};
};

ScsLink::ScsLink() : impl_(std::make_unique<Impl>()) {}
ScsLink::~ScsLink() = default;

uint32_t ScsLink::ack_wait_us() { return ACK_WAIT_US; }

bool ScsLink::enqueue(const ScsFrame &frame, Mode mode) {
  if (impl_->commands.size() == MAX_COMMANDS)
    return false;
  impl_->commands.push_back({frame, mode});
  return true;
}

void ScsLink::note_responder_ack() { impl_->responder_acks++; }

void ScsLink::on_ack(uint32_t timestamp_us) {
  if (impl_->command_active && impl_->command.transaction.accept_ack(timestamp_us, ACK_WAIT_US))
    impl_->finish_command();
  else if (impl_->wire == Impl::Wire::COMMAND && impl_->command_active)
    impl_->early_ack_at_us = timestamp_us;
}

void ScsLink::on_transmit_done(uint32_t transaction_id, uint32_t timestamp_us) {
  if (impl_->wire == Impl::Wire::COMMAND && impl_->command_active &&
      impl_->command.transaction.complete(transaction_id, timestamp_us)) {
    impl_->wire = Impl::Wire::NONE;
    if (impl_->command.mode == Mode::SINGLE)
      impl_->finish_command();
    else if (impl_->command.mode == Mode::NO_ACK) {
      if (impl_->command.transaction.exhausted())
        impl_->finish_command();
      else
        impl_->schedule_command(timestamp_us);
    }
    if (impl_->command.transaction.awaiting_ack() && impl_->early_ack_at_us != NO_EARLY_ACK &&
        impl_->command.transaction.accept_ack(impl_->early_ack_at_us, ACK_WAIT_US))
      impl_->finish_command();
    impl_->early_ack_at_us = NO_EARLY_ACK;
  } else if (impl_->wire == Impl::Wire::RESPONDER && transaction_id == impl_->wire_transaction_id) {
    impl_->wire = Impl::Wire::NONE;
  }
}

void ScsLink::on_collision(Driver &driver, uint32_t now_us) {
  if (impl_->wire == Impl::Wire::NONE)
    return;
  driver.cancel();
  if (impl_->wire == Impl::Wire::RESPONDER) {
    impl_->responder_acks++;
    impl_->responder_access_delay_us = impl_->random_access_delay();
    impl_->responder_next_attempt_us = now_us + impl_->responder_access_delay_us;
  } else if (impl_->command_active) {
    impl_->early_ack_at_us = NO_EARLY_ACK;
    impl_->command.transaction.collided();
    impl_->command.transaction.reset_attempts();
    impl_->schedule_command(now_us, true);
  }
  impl_->wire = Impl::Wire::NONE;
}

void ScsLink::run(Driver &driver, uint32_t now_us) {
  impl_->last_bus_activity_us = driver.last_bus_activity_us();
  if (impl_->wire != Impl::Wire::NONE)
    return;

  if (impl_->responder_acks != 0 && driver.can_transmit() &&
      impl_->access_ready(now_us, impl_->responder_next_attempt_us, impl_->responder_access_delay_us)) {
    impl_->responder_acks--;
    impl_->responder_access_delay_us = 0;
    impl_->start(driver, ScsFrame::acknowledgment(), Impl::Wire::RESPONDER, now_us);
    return;
  }
  if (impl_->command_active && impl_->command.transaction.awaiting_ack()) {
    if (reached(now_us, impl_->command.transaction.complete_at_us() + ACK_WAIT_US)) {
      impl_->command.transaction.collided();
      if (impl_->command.transaction.exhausted())
        impl_->finish_command();
      else
        impl_->schedule_command(now_us);
    }
    return;
  }
  if (!impl_->command_active && !impl_->commands.empty()) {
    impl_->command = impl_->commands.front();
    impl_->commands.erase(impl_->commands.begin());
    impl_->command_active = true;
    impl_->schedule_command(now_us);
  }
  if (!impl_->command_active || !driver.can_transmit() ||
      !impl_->access_ready(now_us, impl_->command.next_attempt_us, impl_->command.access_delay_us))
    return;
  impl_->command.transaction.start_attempt();
  impl_->start(driver, impl_->command.frame, Impl::Wire::COMMAND, now_us);
}

bool ScsLink::command_active() const { return impl_->command_active; }
bool ScsLink::awaiting_ack() const { return impl_->command_active && impl_->command.transaction.awaiting_ack(); }
uint8_t ScsLink::attempts() const { return impl_->command.transaction.attempts(); }
uint32_t ScsLink::pending_responder_acks() const { return impl_->responder_acks; }
void ScsLink::set_random_seed(uint32_t seed) { impl_->random_state = seed == 0 ? 1 : seed; }

bool ScsLink::next_wakeup_us(uint32_t *deadline_us) const {
  if (impl_->wire != Impl::Wire::NONE)
    return false;
  bool has_deadline = false;
  const auto consider = [&has_deadline, deadline_us](uint32_t deadline) {
    if (!has_deadline || static_cast<int32_t>(deadline - *deadline_us) < 0) {
      *deadline_us = deadline;
      has_deadline = true;
    }
  };
  if (impl_->command_active && impl_->command.transaction.awaiting_ack())
    consider(impl_->command.transaction.complete_at_us() + ACK_WAIT_US);
  if (impl_->responder_acks != 0 && impl_->responder_access_delay_us != 0)
    consider(impl_->access_deadline(impl_->responder_next_attempt_us, impl_->responder_access_delay_us));
  if (impl_->command_active && !impl_->command.transaction.awaiting_ack())
    consider(impl_->access_deadline(impl_->command.next_attempt_us, impl_->command.access_delay_us));
  return has_deadline;
}

}  // namespace esphome::scs_bus
