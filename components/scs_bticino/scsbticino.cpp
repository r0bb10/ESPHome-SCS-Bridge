#include "scsbticino.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "esp_err.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#endif

namespace esphome::scs_bticino {

static const char *const TAG = "scs_bticino";
static const char *const TAG_RX = "scs_bticino.rx";
static const char *const TAG_TX = "scs_bticino.tx";

namespace {

const char *tx_type_name(ScsTxType type) {
  switch (type) {
    case ScsTxType::RESPONSE:
      return "response";
    case ScsTxType::SHORT:
      return "short";
    case ScsTxType::EXTENDED:
      return "extended";
    case ScsTxType::EXTENDED_ALT:
      return "extended_alt";
  }
  return "invalid";
}

const char *tx_result_name(ScsTxResult result) {
  switch (result) {
    case ScsTxResult::SUCCESS:
      return "success";
    case ScsTxResult::RESPONSE_TIMEOUT:
      return "response_timeout";
    case ScsTxResult::COLLISION_LIMIT:
      return "collision_limit";
  }
  return "unknown";
}

}  // namespace

void ScsBticinoController::setup() {
#ifndef USE_ESP32
  ESP_LOGE(TAG, "SCS Bticino requires an ESP32 with ESP-IDF RMT support");
  this->mark_failed();
#else
  if (this->rx_pin_ == nullptr || this->tx_pin_ == nullptr) {
    ESP_LOGE(TAG, "Both RX and TX pins are required");
    this->mark_failed(LOG_STR("RMT RX setup failed"));
    return;
  }

  if (!this->receiver_.setup(this->rx_pin_->get_pin())) {
    ESP_LOGE(TAG_RX, "Unable to initialize RMT RX");
    switch (this->receiver_.setup_error()) {
      case ScsBticinoReceiver::SetupError::CHANNEL:
        this->mark_failed(LOG_STR("RMT RX channel allocation failed"));
        break;
      case ScsBticinoReceiver::SetupError::CALLBACK:
        this->mark_failed(LOG_STR("RMT RX callback setup failed"));
        break;
      case ScsBticinoReceiver::SetupError::ENABLE:
        this->mark_failed(LOG_STR("RMT RX enable failed"));
        break;
      case ScsBticinoReceiver::SetupError::RECEIVE_INVALID_ARGUMENT:
        this->mark_failed(LOG_STR("RMT RX start: invalid argument"));
        break;
      case ScsBticinoReceiver::SetupError::RECEIVE_INVALID_STATE:
        this->mark_failed(LOG_STR("RMT RX start: invalid state"));
        break;
      case ScsBticinoReceiver::SetupError::RECEIVE_NO_MEMORY:
        this->mark_failed(LOG_STR("RMT RX start: out of memory"));
        break;
      case ScsBticinoReceiver::SetupError::RECEIVE_OTHER:
        this->mark_failed(LOG_STR("RMT RX start: driver error"));
        break;
      case ScsBticinoReceiver::SetupError::NONE:
        this->mark_failed(LOG_STR("RMT RX setup failed"));
        break;
    }
    return;
  }

  this->tx_pin_->setup();
  const auto tx_gpio = static_cast<gpio_num_t>(this->tx_pin_->get_pin());
  gpio_set_level(tx_gpio, 0);
  gptimer_config_t timer_config{};
  timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  timer_config.direction = GPTIMER_COUNT_UP;
  timer_config.resolution_hz = 1000000;
  timer_config.intr_priority = 3;
  esp_err_t error = gptimer_new_timer(&timer_config, &this->tx_timer_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_TX, "gptimer_new_timer failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPTimer allocation failed"));
    return;
  }
  const gptimer_event_callbacks_t timer_callbacks{.on_alarm = &ScsBticinoController::on_tx_timer_};
  error = gptimer_register_event_callbacks(this->tx_timer_, &timer_callbacks, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_TX, "gptimer_register_event_callbacks failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPTimer callback setup failed"));
    return;
  }
  error = gptimer_enable(this->tx_timer_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_TX, "gptimer_enable failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPTimer enable failed"));
    return;
  }
  const auto rx_gpio = static_cast<gpio_num_t>(this->rx_pin_->get_pin());
  error = gpio_set_intr_type(rx_gpio, GPIO_INTR_NEGEDGE);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_RX, "gpio_set_intr_type failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPIO interrupt type setup failed"));
    return;
  }
  error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG_RX, "gpio_install_isr_service failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPIO ISR service setup failed"));
    return;
  }
  error = gpio_isr_handler_add(rx_gpio, &ScsBticinoController::on_rx_edge_, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_RX, "gpio_isr_handler_add failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPIO ISR handler setup failed"));
  }
#endif
}

bool ScsBticinoController::send(const ScsBticinoData &frame, ScsTxType type) {
#ifndef USE_ESP32
  return false;
#else
  const bool extended = frame.length == SCS_EXTENDED_SIZE;
  const bool type_matches_frame = (type == ScsTxType::EXTENDED || type == ScsTxType::EXTENDED_ALT) ? extended : !extended;
  if (!frame.is_transmittable() || !type_matches_frame) {
    ESP_LOGW(TAG_TX, "TX reject: reason=invalid_frame_or_type type=%s", tx_type_name(type));
    return false;
  }
  if (!this->transmitter_.enqueue(frame, type)) {
    ESP_LOGW(TAG_TX, "TX reject: reason=queue_full depth=%u", this->transmitter_.queue_depth());
    return false;
  }
  ESP_LOGD(TAG_TX, "TX queue: type=%s frame=%s depth=%u", tx_type_name(type), frame.to_string().c_str(),
           this->transmitter_.queue_depth());
  this->start_queued_tx_();
  return true;
#endif
}

#ifdef USE_ESP32
bool ScsBticinoController::start_queued_tx_() {
  if (this->transmitter_.active()) {
    if (this->pending_local_ack_ || this->transmitter_.pending())
      this->set_tx_defer_reason_(1);
    return false;
  }
  if (!this->pending_local_ack_ && !this->transmitter_.pending()) {
    this->set_tx_defer_reason_(0);
    return false;
  }
  if (this->receiver_.bus_busy()) {
    this->set_tx_defer_reason_(2);
    return false;
  }
  if (this->pending_local_ack_)
    return this->start_tx_(true);
  this->set_tx_defer_reason_(0);
  return this->start_tx_(false);
}

bool ScsBticinoController::start_tx_(bool local_ack) {
  if (local_ack ? !this->transmitter_.start_ack() : !this->transmitter_.start_next()) {
    ESP_LOGE(TAG_TX, "TX start failed: reason=%s", local_ack ? "local_ack_state" : "queue_state");
    return false;
  }
  ScsTxStep step{};
  ScsTxResult result{};
  if (!this->transmitter_.advance(false, &step, &result)) {
    this->transmitter_.cancel();
    ESP_LOGE(TAG_TX, "TX start failed: type=%s", tx_type_name(this->transmitter_.type()));
    return false;
  }
  gpio_set_level(static_cast<gpio_num_t>(this->tx_pin_->get_pin()), step.drive_dominant);
  if (!this->arm_tx_timer_(step.delay_us)) {
    this->transmitter_.cancel();
    gpio_set_level(static_cast<gpio_num_t>(this->tx_pin_->get_pin()), 0);
    return false;
  }
  if (local_ack)
    this->pending_local_ack_ = false;
  else
    this->transmitter_.confirm_started();
  ESP_LOGD(TAG_TX, "TX schedule: type=%s frame=%s local_ack=%s queue=%u", tx_type_name(this->transmitter_.type()),
           this->transmitter_.frame().to_string().c_str(), local_ack ? "yes" : "no", this->transmitter_.queue_depth());
  return true;
}

void ScsBticinoController::push_tx_trace_(uint8_t kind) {
  const uint8_t next = (this->tx_trace_write_ + 1) % TX_TRACE_CAPACITY;
  if (next == this->tx_trace_read_) {
    this->tx_trace_overflow_ = true;
    this->tx_trace_dropped_++;
    return;
  }
  auto &trace = this->tx_traces_[this->tx_trace_write_];
  const auto &frame = this->transmitter_.frame();
  for (uint8_t index = 0; index < frame.length; index++)
    trace.bytes[index] = frame.bytes[index];
  trace.length = frame.length;
  trace.kind = kind;
  trace.type = static_cast<uint8_t>(this->transmitter_.type());
  trace.attempts = this->transmitter_.attempts();
  trace.collisions = this->transmitter_.collisions();
  trace.local_ack = this->transmitter_.local_ack();
  this->tx_trace_write_ = next;
}

void ScsBticinoController::drain_tx_traces_() {
  if (this->tx_trace_overflow_) {
    const uint16_t dropped = this->tx_trace_dropped_;
    this->tx_trace_overflow_ = false;
    this->tx_trace_dropped_ = 0;
    ESP_LOGW(TAG_TX, "TX trace overflow: dropped=%u", dropped);
  }
  while (this->tx_trace_read_ != this->tx_trace_write_) {
    const TxTrace trace = this->tx_traces_[this->tx_trace_read_];
    ScsBticinoData frame;
    frame.length = trace.length;
    for (uint8_t index = 0; index < trace.length; index++)
      frame.bytes[index] = trace.bytes[index];
    this->tx_trace_read_ = (this->tx_trace_read_ + 1) % TX_TRACE_CAPACITY;
    if (trace.kind == TX_TRACE_ATTEMPT) {
      ESP_LOGI(TAG_TX, "TX %s", frame.to_string().c_str());
      ESP_LOGD(TAG_TX, "TX attempt: type=%s number=%u collisions=%u local_ack=%s",
               trace.local_ack ? "local_ack" : tx_type_name(static_cast<ScsTxType>(trace.type)),
               trace.attempts, trace.collisions, trace.local_ack ? "yes" : "no");
    } else if (trace.kind == TX_TRACE_COLLISION) {
      ESP_LOGD(TAG_TX, "TX collision: frame=%s count=%u", frame.to_string().c_str(), trace.collisions);
    } else {
      ESP_LOGD(TAG_TX, "TX wait_response: attempt=%u", trace.attempts);
    }
  }
}

void ScsBticinoController::set_tx_defer_reason_(uint8_t reason) {
  if (this->tx_defer_reason_ == reason)
    return;
  this->tx_defer_reason_ = reason;
  if (reason == 1) {
    ESP_LOGD(TAG_TX, "TX defer: reason=active");
  } else if (reason == 2) {
    ESP_LOGD(TAG_TX, "TX defer: reason=bus_busy depth=%u", this->transmitter_.queue_depth());
  }
}

bool ScsBticinoController::is_locally_addressed_(const ScsBticinoData &frame) const {
  const uint8_t system = this->identity_.system & 0x0F;
  if (!frame.is_valid() || system == 0 || (frame.bytes[3] >> 4) != system)
    return false;
  if (system == 1 || system == 4)
    return this->identity_.address == frame.bytes[1];
  return (frame.bytes[1] & 0xF0) == 0x80 && (this->identity_.address & 0x0FFF) ==
                                                ((frame.bytes[3] & 0x0F) << 8 | frame.bytes[2]);
}
#endif

void ScsBticinoController::loop() {
#ifdef USE_ESP32
  this->drain_tx_traces_();
  if (this->tx_timer_fault_) {
    this->tx_timer_fault_ = false;
    ESP_LOGE(TAG_TX, "TX timer re-arm failed: %s; transaction cancelled", esp_err_to_name(this->tx_timer_error_));
  }
  if (this->tx_result_ready_) {
    this->tx_result_ready_ = false;
    const char *result = tx_result_name(this->tx_result_);
    const char *type = this->tx_result_local_ack_ ? "local_ack" : tx_type_name(this->transmitter_.type());
    if (this->tx_result_ == ScsTxResult::SUCCESS) {
      ESP_LOGD(TAG_TX, "TX result: %s type=%s attempts=%u collisions=%u", result, type,
               this->transmitter_.attempts(), this->transmitter_.collisions());
    } else {
      ESP_LOGW(TAG_TX, "TX result: %s type=%s attempts=%u collisions=%u", result, type,
               this->transmitter_.attempts(), this->transmitter_.collisions());
    }
  }
  ScsBticinoData frame;
  if (this->receiver_.poll(&frame)) {
    ESP_LOGI(TAG_RX, "RX %s", frame.to_string().c_str());
    if (this->telegram_sensor_ != nullptr)
      this->telegram_sensor_->publish_state(frame.to_string());
    if (frame.is_ack() && this->transmitter_.state() == ScsTxState::WAIT_RESPONSE) {
      ScsTxResult result{};
      if (this->transmitter_.complete_response(&result)) {
        gptimer_stop(this->tx_timer_);
        this->tx_result_ = result;
        this->tx_result_local_ack_ = false;
        this->tx_result_ready_ = true;
        ESP_LOGD(TAG_RX, "RX A5: complete type-0 response");
      }
    } else if (this->is_locally_addressed_(frame)) {
      this->pending_local_ack_ = true;
      ESP_LOGD(TAG_RX, "RX local address: queue A5 response");
    }
  }
  this->start_queued_tx_();
#endif
}

#ifdef USE_ESP32
bool ScsBticinoController::arm_tx_timer_(uint64_t alarm_us) {
  esp_err_t error = gptimer_stop(this->tx_timer_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_TX, "TX timer stop failed: %s", esp_err_to_name(error));
    return false;
  }
  error = gptimer_set_raw_count(this->tx_timer_, 0);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_TX, "TX timer counter reset failed: %s", esp_err_to_name(error));
    return false;
  }
  const gptimer_alarm_config_t config{.alarm_count = alarm_us, .flags = {.auto_reload_on_alarm = false}};
  error = gptimer_set_alarm_action(this->tx_timer_, &config);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_TX, "TX timer alarm setup failed: %s", esp_err_to_name(error));
    return false;
  }
  error = gptimer_start(this->tx_timer_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG_TX, "TX timer start failed: %s", esp_err_to_name(error));
  }
  return error == ESP_OK;
}

bool IRAM_ATTR ScsBticinoController::on_tx_timer_(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event,
                                                    void *arg) {
  auto *controller = static_cast<ScsBticinoController *>(arg);
  ScsTxStep step{};
  ScsTxResult result{};
  const ScsTxState previous_state = controller->transmitter_.state();
  const bool checking_released = controller->transmitter_.checking_released();
  const bool local_ack = controller->transmitter_.local_ack();
  const bool rx_dominant = controller->transmitter_.awaiting_access()
                               ? controller->access_contended_
                                : gpio_get_level(static_cast<gpio_num_t>(controller->rx_pin_->get_pin())) == 0;
  controller->access_contended_ = false;
  const bool active = controller->transmitter_.advance(rx_dominant, &step, &result);
  if (checking_released && rx_dominant)
    controller->push_tx_trace_(TX_TRACE_COLLISION);
  if (previous_state == ScsTxState::WAIT_ACCESS && controller->transmitter_.state() == ScsTxState::START)
    controller->push_tx_trace_(TX_TRACE_ATTEMPT);
  if (previous_state == ScsTxState::END && controller->transmitter_.state() == ScsTxState::WAIT_RESPONSE)
    controller->push_tx_trace_(TX_TRACE_WAIT_RESPONSE);
  if (!active) {
    gpio_set_level(static_cast<gpio_num_t>(controller->tx_pin_->get_pin()), 0);
    controller->tx_result_ = result;
    controller->tx_result_local_ack_ = local_ack;
    controller->tx_result_ready_ = true;
    return false;
  }
  gpio_set_level(static_cast<gpio_num_t>(controller->tx_pin_->get_pin()), step.drive_dominant);
  const gptimer_alarm_config_t config{.alarm_count = event->alarm_value + step.delay_us,
                                       .flags = {.auto_reload_on_alarm = false}};
  const esp_err_t error = gptimer_set_alarm_action(timer, &config);
  if (error != ESP_OK) {
    controller->transmitter_.cancel();
    gpio_set_level(static_cast<gpio_num_t>(controller->tx_pin_->get_pin()), 0);
    controller->tx_timer_error_ = error;
    controller->tx_timer_fault_ = true;
  }
  return false;
}

void IRAM_ATTR ScsBticinoController::on_rx_edge_(void *arg) {
  auto *controller = static_cast<ScsBticinoController *>(arg);
  controller->receiver_.on_bus_edge();
  // Local TX dominant edges are visible on RX on some F422 interfaces. Only
  // an edge while waiting to claim an idle bus can cancel random access.
  if (controller->transmitter_.awaiting_access())
    controller->access_contended_ = true;
}
#endif

void ScsBticinoController::dump_config() {
  ESP_LOGCONFIG(TAG, "SCS Bticino:");
  LOG_PIN("  RX Pin: ", this->rx_pin_);
  LOG_PIN("  TX Pin: ", this->tx_pin_);
  ESP_LOGCONFIG(TAG, "  RMT resolution: 1 MHz");
  ESP_LOGCONFIG(TAG, "  TX polarity/released level: normal/low (F422 interface)");
}

}  // namespace esphome::scs_bticino
