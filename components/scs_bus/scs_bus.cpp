#include "scs_bus.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace scs_bus {

static const char *const TAG = "scs_bus";
static constexpr size_t MAX_TRANSMIT_QUEUE = 8;
static constexpr uint32_t BUS_IDLE_GUARD_MS = 2;

void ScsBus::setup() {
  if (rx_pin_ < 0 || tx_pin_ < 0) {
    ESP_LOGE(TAG, "Both RX and TX pins are required");
    mark_failed();
    return;
  }

  rmt_.set_receive_callback(&ScsBus::on_byte_, this);
  rmt_.set_transmit_done_callback(&ScsBus::on_transmit_done_, this);
  const esp_err_t err = rmt_.setup(rx_pin_, tx_pin_, rx_inverted_, tx_inverted_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize RMT: %s", esp_err_to_name(err));
    mark_failed();
  }
}

void ScsBus::loop() {
  if (is_failed())
    return;

  rmt_.loop();
  if (awaiting_ack_ && millis() >= ack_deadline_)
    fail_or_retry_();
  if (!active_tx_valid_)
    start_next_transmission_();
}

void ScsBus::dump_config() {
  ESP_LOGCONFIG(TAG, "SCS Bus:");
  ESP_LOGCONFIG(TAG, "  RX Pin: GPIO%d%s", rx_pin_, rx_inverted_ ? " (inverted)" : "");
  ESP_LOGCONFIG(TAG, "  TX Pin: GPIO%d%s", tx_pin_, tx_inverted_ ? " (inverted)" : "");
  ESP_LOGCONFIG(TAG, "  ACK timeout: %lu ms", static_cast<unsigned long>(ack_timeout_ms_));
  ESP_LOGCONFIG(TAG, "  Max retries: %u", max_retries_);
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
  if (transmit_queue_.size() >= MAX_TRANSMIT_QUEUE) {
    queue_overflows_++;
    ESP_LOGW(TAG, "Transmit queue full; dropping door unlock for address 0x%02X", address);
    return false;
  }
  transmit_queue_.push_back(PendingTx{frame, 0});
  return true;
}

void ScsBus::on_byte_(void *context, uint8_t byte) { static_cast<ScsBus *>(context)->handle_byte_(byte); }

void ScsBus::on_transmit_done_(void *context) {
  auto *bus = static_cast<ScsBus *>(context);
  if (!bus->active_tx_valid_)
    return;
  bus->awaiting_ack_ = true;
  bus->ack_deadline_ = millis() + bus->ack_timeout_ms_;
}

void ScsBus::handle_byte_(uint8_t byte) {
  last_bus_activity_ms_ = millis();
  ScsFrame frame;
  const ScsParseResult result = assembler_.push(byte, frame);
  if (result == ScsParseResult::INVALID) {
    invalid_frames_++;
    ESP_LOGW(TAG, "Dropped invalid native SCS frame");
    return;
  }
  if (result == ScsParseResult::FRAME)
    handle_frame_(frame);
}

void ScsBus::handle_frame_(const ScsFrame &frame) {
  if (frame.is_ack()) {
    if (awaiting_ack_) {
      awaiting_ack_ = false;
      active_tx_valid_ = false;
      ESP_LOGD(TAG, "Received native SCS ACK");
    }
    return;
  }

  received_frames_++;
  publish_frame_(frame);
  DoorbellEvent event;
  if (scs_decode_doorbell(frame, event)) {
    ESP_LOGD(TAG, "Doorbell event for address 0x%02X", event.address);
    on_doorbell_callbacks_.call(event.address);
  }
}

void ScsBus::start_next_transmission_() {
  if (rmt_.transmitting() || transmit_queue_.empty() || millis() - last_bus_activity_ms_ < BUS_IDLE_GUARD_MS)
    return;

  active_tx_ = transmit_queue_.front();
  transmit_queue_.erase(transmit_queue_.begin());
  active_tx_valid_ = true;
  active_tx_.attempts++;
  const esp_err_t err = rmt_.transmit(active_tx_.frame.bytes, active_tx_.frame.size());
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not transmit native SCS frame: %s", esp_err_to_name(err));
    fail_or_retry_();
  }
}

void ScsBus::fail_or_retry_() {
  awaiting_ack_ = false;
  ack_timeouts_++;
  if (active_tx_.attempts <= max_retries_) {
    retries_++;
    ESP_LOGW(TAG, "SCS ACK timeout; retrying (%u/%u)", active_tx_.attempts, max_retries_);
    transmit_queue_.insert(transmit_queue_.begin(), active_tx_);
  } else {
    ESP_LOGW(TAG, "SCS ACK timeout; command failed after %u attempts", active_tx_.attempts);
  }
  active_tx_valid_ = false;
}

void ScsBus::publish_frame_(const ScsFrame &frame) {
  std::vector<uint8_t> bytes(frame.bytes, frame.bytes + frame.size());
  on_frame_callbacks_.call(bytes);
}

}  // namespace scs_bus
}  // namespace esphome
