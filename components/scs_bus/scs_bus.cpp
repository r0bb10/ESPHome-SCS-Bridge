#include "scs_bus.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace scs_bus {

static const char *const TAG = "scs_bus";
class TransportDriver final : public ScsLink::Driver {
 public:
  explicit TransportDriver(ScsTransport &transport) : transport_(transport) {}
  bool can_transmit() const override { return transport_.can_transmit(); }
  uint32_t last_bus_activity_us() const override { return transport_.last_bus_activity_us(); }
  bool transmit(const ScsFrame &frame, uint32_t id) override {
    return transport_.transmit(frame.bytes, frame.size(), id) == ESP_OK;
  }
  void cancel() override { transport_.cancel_transmit(); }
 private:
  ScsTransport &transport_;
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
  frame_queue_ = xQueueCreate(16, sizeof(ScsFrame));
  if (command_queue_ == nullptr || frame_queue_ == nullptr ||
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
  ScsFrame frame;
  while (xQueueReceive(frame_queue_, &frame, 0) == pdTRUE)
    handle_frame_(frame);
}

void ScsBus::dump_config() {
  ESP_LOGCONFIG(TAG, "SCS Bus:");
  ESP_LOGCONFIG(TAG, "  RX Pin: GPIO%d%s", rx_pin_, rx_inverted_ ? " (inverted)" : "");
  ESP_LOGCONFIG(TAG, "  TX Pin: GPIO%d%s", tx_pin_, tx_inverted_ ? " (inverted)" : "");
  ESP_LOGCONFIG(TAG, "  OEM ACK timeout: %lu us", static_cast<unsigned long>(ScsLink::ack_wait_us()));
}

void ScsBus::add_doorbell_listener(uint8_t address, std::function<void()> &&callback) {
  on_doorbell_callbacks_.add([address, callback = std::move(callback)](uint8_t received_address) {
    if (received_address == address)
      callback();
  });
}

bool ScsBus::send_door_unlock(uint8_t address) {
  ScsFrame frame;
  if (!scs_build_door_unlock(DoorUnlock{address}, frame))
    return false;
  const CommandRequest request{frame, ScsLink::Mode::ACK};
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
  TransportDriver driver(transport_);
  for (;;) {
    CommandRequest request;
    while (xQueueReceive(command_queue_, &request, 0) == pdTRUE) {
      if (!link_.enqueue(request.frame, request.mode))
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
  ScsFrame frame;
  const ScsParseResult result = tx_assembler_.push(event.byte, frame);
  if (result == ScsParseResult::INVALID) {
    invalid_frames_++;
    ESP_LOGW(TAG, "Dropped invalid native SCS frame");
  } else if (result == ScsParseResult::FRAME) {
    if (frame.is_ack())
      link_.on_ack(event.timestamp_us);
    else {
      if (is_addressed_(frame))
        link_.note_responder_ack();
      if (xQueueSend(frame_queue_, &frame, 0) != pdTRUE)
        queue_overflows_++;
    }
  }
}

void ScsBus::handle_frame_(const ScsFrame &frame) {
  if (frame.is_ack())
    return;
  received_frames_++;
  publish_frame_(frame);
  DoorbellEvent event;
  if (scs_decode_doorbell(frame, event)) {
    ESP_LOGD(TAG, "Doorbell event for address 0x%02X", event.address);
    on_doorbell_callbacks_.call(event.address);
  }
}


bool ScsBus::is_addressed_(const ScsFrame &frame) const {
  if (local_system_ == 0 || !frame.is_valid() || frame.is_ack())
    return false;
  const uint8_t *payload = frame.payload();
  if ((payload[2] & 0xF0U) != static_cast<uint8_t>(local_system_ << 4U))
    return false;
  if (local_system_ == 1 || local_system_ == 4)
    return static_cast<uint8_t>(local_address_) == payload[0];
  return (payload[0] & 0xF0U) == 0x80U &&
         local_address_ == static_cast<uint16_t>(((payload[2] & 0x0FU) << 8U) | payload[1]);
}


void ScsBus::publish_frame_(const ScsFrame &frame) {
  std::vector<uint8_t> bytes(frame.bytes, frame.bytes + frame.size());
  on_frame_callbacks_.call(bytes);
}

}  // namespace scs_bus
}  // namespace esphome
