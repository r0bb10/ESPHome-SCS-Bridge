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
  enum class Diagnostics : uint8_t { OFF, TELEGRAMS, VERBOSE };
  void set_diagnostics(uint8_t diagnostics) { diagnostics_ = static_cast<Diagnostics>(diagnostics); }
  void add_on_telegram_callback(std::function<void(std::vector<uint8_t>)> &&callback) {
    on_telegram_callbacks_.add(std::move(callback));
  }
  void add_doorbell_listener(uint8_t address, std::function<void()> &&callback);
  bool send_door_unlock(uint8_t address);

 protected:
  struct CommandRequest { ScsTelegram telegram; ScsLink::Mode mode; };
  enum class DiagnosticEventType : uint8_t { RX_BYTE, RX_FRAMING_ERROR, RX_TELEGRAM, RX_INVALID_TELEGRAM, TX_TELEGRAM, COLLISION };
  struct DiagnosticEvent {
    DiagnosticEventType type;
    ScsTransport::RxEvent byte{};
    ScsTelegram telegram{};
  };

  static void tx_task_(void *context);
  void run_tx_task_();
  void handle_rx_in_task_(const ScsTransport::RxEvent &event);
  void handle_diagnostic_event_(const DiagnosticEvent &event);
  void handle_telegram_(const ScsTelegram &telegram);
  bool is_addressed_(const ScsTelegram &telegram) const;
  void publish_telegram_(const ScsTelegram &telegram);
  void queue_rx_byte_(const ScsTransport::RxEvent &event);
  void queue_rx_framing_error_(const ScsTransport::RxEvent &event);
  void queue_rx_telegram_(const ScsTelegram &telegram, bool valid);
  void queue_tx_telegram_(const ScsTelegram &telegram);
  void queue_collision_();
  void queue_diagnostic_event_(const DiagnosticEvent &event);

  int rx_pin_{-1};
  int tx_pin_{-1};
  bool rx_inverted_{false};
  bool tx_inverted_{false};
  Diagnostics diagnostics_{Diagnostics::OFF};
  uint8_t local_system_{0};
  uint16_t local_address_{0};
  ScsTransport transport_;
  ScsTelegramAssembler telegram_assembler_;
  ScsLink link_;
  QueueHandle_t command_queue_{nullptr};
  QueueHandle_t telegram_queue_{nullptr};
  QueueHandle_t diagnostic_queue_{nullptr};
  TaskHandle_t tx_task_handle_{nullptr};
  CallbackManager<void(std::vector<uint8_t>)> on_telegram_callbacks_;
  CallbackManager<void(uint8_t)> on_doorbell_callbacks_;
  std::atomic<uint32_t> received_telegrams_{0};
  std::atomic<uint32_t> invalid_telegrams_{0};
  std::atomic<uint32_t> collisions_{0};
  std::atomic<uint32_t> queue_overflows_{0};
  std::atomic<uint32_t> diagnostic_queue_overflows_{0};
};

}  // namespace scs_bus
}  // namespace esphome
