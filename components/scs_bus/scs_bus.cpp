#include "scs_bus.h"

#include <cstdio>
#include <functional>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::scs_bus {

static const char *const TAG = "scs_bus";

class TransportDriver final : public ScsLink::Driver {
 public:
  TransportDriver(ScsTransport &transport, std::function<void(const ScsTelegram &)> on_transmit)
      : transport_(transport), on_transmit_(std::move(on_transmit)) {}
  bool can_transmit() const override { return transport_.can_transmit(); }
  uint32_t last_bus_activity_us() const override { return transport_.last_bus_activity_us(); }
  bool transmit(const ScsTelegram &telegram, uint32_t id) override {
    if (transport_.transmit(telegram.bytes, telegram.size(), id) != ESP_OK)
      return false;
    on_transmit_(telegram);
    return true;
  }
  void cancel() override { transport_.cancel_transmit(); }

 private:
  ScsTransport &transport_;
  std::function<void(const ScsTelegram &)> on_transmit_;
};

void ScsBus::setup() {
  if (rx_pin_ < 0 || tx_pin_ < 0) {
    ESP_LOGE(TAG, "Both RX and TX pins are required");
    mark_failed();
    return;
  }
  const esp_err_t err = transport_.setup(rx_pin_, tx_pin_, rx_inverted_, tx_inverted_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SCS transport: %s", esp_err_to_name(err));
    mark_failed();
    return;
  }
  command_queue_ = xQueueCreate(8, sizeof(CommandRequest));
  telegram_queue_ = xQueueCreate(16, sizeof(ScsTelegram));
  if (diagnostics_ != Diagnostics::OFF)
    diagnostic_queue_ = xQueueCreate(64, sizeof(DiagnosticEvent));
  if (command_queue_ == nullptr || telegram_queue_ == nullptr ||
      (diagnostics_ != Diagnostics::OFF && diagnostic_queue_ == nullptr) ||
      xTaskCreate(&ScsBus::tx_task_, "scs_tx", 4096, this, configMAX_PRIORITIES - 1, &tx_task_handle_) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create SCS TX coordinator task");
    mark_failed();
    return;
  }
  transport_.set_event_task(tx_task_handle_);
}

void ScsBus::loop() {
  if (is_failed())
    return;
  ScsTelegram telegram;
  while (xQueueReceive(telegram_queue_, &telegram, 0) == pdTRUE)
    handle_telegram_(telegram);
  DiagnosticEvent event;
  while (diagnostic_queue_ != nullptr && xQueueReceive(diagnostic_queue_, &event, 0) == pdTRUE)
    handle_diagnostic_event_(event);
  const uint32_t dropped = diagnostic_queue_overflows_.exchange(0, std::memory_order_relaxed);
  if (dropped != 0)
    ESP_LOGW(TAG, "Dropped %lu diagnostic events", static_cast<unsigned long>(dropped));
}

void ScsBus::dump_config() {
  ESP_LOGCONFIG(TAG, "SCS Bus:");
  ESP_LOGCONFIG(TAG, "  RX Pin: GPIO%d%s", rx_pin_, rx_inverted_ ? " (inverted)" : "");
  ESP_LOGCONFIG(TAG, "  TX Pin: GPIO%d%s", tx_pin_, tx_inverted_ ? " (inverted)" : "");
  const char *diagnostics = diagnostics_ == Diagnostics::OFF ? "off" :
                            diagnostics_ == Diagnostics::TELEGRAMS ? "telegrams" : "verbose";
  ESP_LOGCONFIG(TAG, "  Diagnostics: %s", diagnostics);
}

void ScsBus::add_doorbell_listener(uint8_t address, std::function<void()> &&callback) {
  on_doorbell_callbacks_.add([address, callback = std::move(callback)](uint8_t received_address) {
    if (received_address == address)
      callback();
  });
}

bool ScsBus::send_door_unlock(uint8_t address) {
  ScsTelegram telegram;
  if (!scs_build_door_unlock(DoorUnlock{address}, telegram))
    return false;
  const CommandRequest request{telegram, ScsLink::Mode::ACK};
  if (command_queue_ == nullptr || xQueueSend(command_queue_, &request, 0) != pdTRUE) {
    queue_overflows_++;
    ESP_LOGW(TAG, "Transmit queue full; dropping door unlock for address 0x%02X", address);
    return false;
  }
  xTaskNotifyGive(tx_task_handle_);
  return true;
}

void ScsBus::tx_task_(void *context) { static_cast<ScsBus *>(context)->run_tx_task_(); }

void ScsBus::run_tx_task_() {
  TransportDriver driver(transport_, [this](const ScsTelegram &telegram) { queue_tx_telegram_(telegram); });
  for (;;) {
    CommandRequest request;
    while (xQueueReceive(command_queue_, &request, 0) == pdTRUE) {
      if (!link_.enqueue(request.telegram, request.mode))
        queue_overflows_++;
    }
    ScsTransport::RxEvent event;
    while (transport_.take_rx_event(&event))
      handle_rx_in_task_(event);
    uint32_t transaction_id;
    uint32_t completed_at_us;
    while (transport_.take_transmit_done(&transaction_id, &completed_at_us))
      link_.on_transmit_done(transaction_id, completed_at_us);
    if (transport_.take_collision()) {
      collisions_++;
      queue_collision_();
      link_.on_collision(driver, micros());
    }
    const uint32_t now_us = micros();
    link_.run(driver, now_us);
    uint32_t wake_at_us;
    if (link_.next_wakeup_us(&wake_at_us))
      transport_.arm_event_task_in(static_cast<uint32_t>(wake_at_us - now_us));
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}

void ScsBus::handle_rx_in_task_(const ScsTransport::RxEvent &event) {
  if (event.type == ScsTransport::RxEvent::Type::FRAMING_ERROR) {
    queue_rx_framing_error_(event);
    return;
  }
  queue_rx_byte_(event);
  ScsTelegram telegram;
  const ScsTelegramParseResult result = telegram_assembler_.push(event.byte, telegram);
  if (result == ScsTelegramParseResult::INVALID) {
    invalid_telegrams_++;
    queue_rx_telegram_(telegram, false);
  } else if (result == ScsTelegramParseResult::TELEGRAM) {
    queue_rx_telegram_(telegram, true);
    if (telegram.is_ack())
      link_.on_ack(event.timestamp_us);
    else {
      if (is_addressed_(telegram))
        link_.note_responder_ack();
      if (xQueueSend(telegram_queue_, &telegram, 0) != pdTRUE)
        queue_overflows_++;
    }
  }
}

void ScsBus::handle_diagnostic_event_(const DiagnosticEvent &event) {
  if (event.type == DiagnosticEventType::RX_BYTE) {
    ESP_LOGI(TAG, "RX byte #%lu at %lu us: %02X", static_cast<unsigned long>(event.byte.sequence),
             static_cast<unsigned long>(event.byte.timestamp_us), event.byte.byte);
    return;
  }
  if (event.type == DiagnosticEventType::RX_FRAMING_ERROR) {
    ESP_LOGW(TAG, "RX framing error #%lu at %lu us", static_cast<unsigned long>(event.byte.sequence),
             static_cast<unsigned long>(event.byte.timestamp_us));
    return;
  }
  if (event.type == DiagnosticEventType::COLLISION) {
    ESP_LOGW(TAG, "TX collision detected");
    return;
  }
  const size_t length = event.telegram.size();
  char hex[SCS_EXTENDED_TELEGRAM_SIZE * 3 + 1]{};
  size_t offset = 0;
  for (size_t index = 0; index < length; index++)
    offset += snprintf(hex + offset, sizeof(hex) - offset, "%s%02X", index == 0 ? "" : " ", event.telegram.bytes[index]);
  if (event.type == DiagnosticEventType::TX_TELEGRAM)
    ESP_LOGI(TAG, "TX SCS %s: %s", event.telegram.is_ack() ? "ACK" : "telegram", hex);
  else if (event.type == DiagnosticEventType::RX_INVALID_TELEGRAM)
    ESP_LOGW(TAG, "RX invalid SCS telegram candidate: %s", hex);
  else
    ESP_LOGI(TAG, "RX SCS %s: %s", event.telegram.is_ack() ? "ACK" : "telegram", hex);
}

void ScsBus::handle_telegram_(const ScsTelegram &telegram) {
  received_telegrams_++;
  publish_telegram_(telegram);
  DoorbellEvent event;
  if (scs_decode_doorbell(telegram, event))
    on_doorbell_callbacks_.call(event.address);
}

bool ScsBus::is_addressed_(const ScsTelegram &telegram) const {
  if (local_system_ == 0 || !telegram.is_valid() || telegram.is_ack())
    return false;
  const uint8_t *payload = telegram.payload();
  if ((payload[2] & 0xF0U) != static_cast<uint8_t>(local_system_ << 4U))
    return false;
  if (local_system_ == 1 || local_system_ == 4)
    return static_cast<uint8_t>(local_address_) == payload[0];
  return (payload[0] & 0xF0U) == 0x80U &&
         local_address_ == static_cast<uint16_t>(((payload[2] & 0x0FU) << 8U) | payload[1]);
}

void ScsBus::publish_telegram_(const ScsTelegram &telegram) {
  std::vector<uint8_t> bytes(telegram.bytes, telegram.bytes + telegram.size());
  on_telegram_callbacks_.call(bytes);
}

void ScsBus::queue_rx_byte_(const ScsTransport::RxEvent &event) {
  if (diagnostics_ == Diagnostics::VERBOSE)
    queue_diagnostic_event_({DiagnosticEventType::RX_BYTE, event});
}

void ScsBus::queue_rx_framing_error_(const ScsTransport::RxEvent &event) {
  if (diagnostics_ == Diagnostics::VERBOSE)
    queue_diagnostic_event_({DiagnosticEventType::RX_FRAMING_ERROR, event});
}

void ScsBus::queue_rx_telegram_(const ScsTelegram &telegram, bool valid) {
  if (diagnostics_ == Diagnostics::OFF || (!valid && diagnostics_ != Diagnostics::VERBOSE))
    return;
  queue_diagnostic_event_({valid ? DiagnosticEventType::RX_TELEGRAM : DiagnosticEventType::RX_INVALID_TELEGRAM, {}, telegram});
}

void ScsBus::queue_tx_telegram_(const ScsTelegram &telegram) {
  if (diagnostics_ != Diagnostics::OFF)
    queue_diagnostic_event_({DiagnosticEventType::TX_TELEGRAM, {}, telegram});
}

void ScsBus::queue_collision_() {
  if (diagnostics_ == Diagnostics::VERBOSE)
    queue_diagnostic_event_({DiagnosticEventType::COLLISION});
}

void ScsBus::queue_diagnostic_event_(const DiagnosticEvent &event) {
  if (xQueueSend(diagnostic_queue_, &event, 0) != pdTRUE)
    diagnostic_queue_overflows_++;
}

}  // namespace esphome::scs_bus
