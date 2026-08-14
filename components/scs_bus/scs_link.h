#pragma once

#include <cstdint>
#include <memory>

#include "scs_codec.h"

namespace esphome::scs_bus {

// The only policy object allowed to start or cancel a physical transmission.
class ScsLink {
 public:
  enum class Mode : uint8_t { SINGLE, NO_ACK, ACK };

  class Driver {
   public:
    virtual ~Driver() = default;
    // Access timing is applied against last_bus_activity_us(), not here.
    virtual bool can_transmit() const = 0;
    virtual uint32_t last_bus_activity_us() const = 0;
    virtual bool transmit(const ScsTelegram &telegram, uint32_t transaction_id) = 0;
    virtual void cancel() = 0;
  };

  ScsLink();
  ~ScsLink();

  bool enqueue(const ScsTelegram &telegram, Mode mode = Mode::ACK);
  void note_responder_ack();
  void on_ack(uint32_t timestamp_us);
  void on_transmit_done(uint32_t transaction_id, uint32_t timestamp_us);
  void on_collision(Driver &driver, uint32_t now_us);
  void run(Driver &driver, uint32_t now_us);

  static uint32_t ack_wait_us();
  bool command_active() const;
  bool awaiting_ack() const;
  uint8_t attempts() const;
  uint32_t pending_responder_acks() const;
  void set_random_seed(uint32_t seed);
  bool next_wakeup_us(uint32_t *deadline_us) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace esphome::scs_bus
