#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esphome/core/component.h"
#include "scs_codec.h"
#include "scs_own.h"
#include "scs_transport.h"
#include "scs_link.h"

namespace esphome {
namespace scs_bus {

class ScsBus : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_rx_pin(int pin) { rx_pin_ = pin; }
  void set_tx_pin(int pin) { tx_pin_ = pin; }
  void set_rx_inverted(bool inverted) { rx_inverted_ = inverted; }
  void set_tx_inverted(bool inverted) { tx_inverted_ = inverted; }
  void set_local_system(uint8_t system) { local_system_ = system; }
  void set_local_address(uint16_t address) { local_address_ = address; }
  void add_on_frame_callback(std::function<void(std::vector<uint8_t>)> &&callback) {
    on_frame_callbacks_.add(std::move(callback));
  }
  void add_doorbell_listener(uint8_t address, std::function<void()> &&callback);
  bool send_door_unlock(uint8_t address);

 protected:
  struct CommandRequest { ScsFrame frame; ScsLink::Mode mode; };
  static void tx_task_(void *context);
  void run_tx_task_();
  void handle_rx_in_task_(const ScsTransport::RxEvent &event);
  void handle_frame_(const ScsFrame &frame);
  bool is_addressed_(const ScsFrame &frame) const;
  void publish_frame_(const ScsFrame &frame);

  int rx_pin_{-1};
  int tx_pin_{-1};
  bool rx_inverted_{false};
  bool tx_inverted_{false};
  uint8_t local_system_{0};
  uint16_t local_address_{0};
  ScsTransport transport_;
  ScsFrameAssembler tx_assembler_;
  ScsLink link_;
  QueueHandle_t command_queue_{nullptr};
  QueueHandle_t frame_queue_{nullptr};
  TaskHandle_t tx_task_handle_{nullptr};
  CallbackManager<void(std::vector<uint8_t>)> on_frame_callbacks_;
  CallbackManager<void(uint8_t)> on_doorbell_callbacks_;
  std::atomic<uint32_t> received_frames_{0};
  std::atomic<uint32_t> invalid_frames_{0};
  std::atomic<uint32_t> collisions_{0};
  std::atomic<uint32_t> queue_overflows_{0};
};

}  // namespace scs_bus
}  // namespace esphome
