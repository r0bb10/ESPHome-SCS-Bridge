#include "scsbticino.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "esp_err.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#endif

namespace esphome::scs_bticino {

static const char *const TAG = "scs_bticino";

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
    ESP_LOGE(TAG, "Unable to initialize RMT RX");
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
    ESP_LOGE(TAG, "gptimer_new_timer failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPTimer allocation failed"));
    return;
  }
  const gptimer_event_callbacks_t timer_callbacks{.on_alarm = &ScsBticinoController::on_tx_timer_};
  error = gptimer_register_event_callbacks(this->tx_timer_, &timer_callbacks, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "gptimer_register_event_callbacks failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPTimer callback setup failed"));
    return;
  }
  error = gptimer_enable(this->tx_timer_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "gptimer_enable failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPTimer enable failed"));
    return;
  }
  const auto rx_gpio = static_cast<gpio_num_t>(this->rx_pin_->get_pin());
  error = gpio_set_intr_type(rx_gpio, GPIO_INTR_NEGEDGE);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "gpio_set_intr_type failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPIO interrupt type setup failed"));
    return;
  }
  error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPIO ISR service setup failed"));
    return;
  }
  error = gpio_isr_handler_add(rx_gpio, &ScsBticinoController::on_rx_edge_, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(error));
    this->mark_failed(LOG_STR("GPIO ISR handler setup failed"));
  }
#endif
}

bool ScsBticinoController::send(const ScsBticinoData &frame, ScsTxType type) {
#ifndef USE_ESP32
  return false;
#else
  if (!this->transmitter_.enqueue(frame, type)) {
    ESP_LOGW(TAG, "TX rejected: scheduler busy");
    return false;
  }
  ScsTxStep step{};
  ScsTxResult result{};
  if (!this->transmitter_.advance(false, &step, &result)) {
    this->transmitter_.cancel();
    ESP_LOGE(TAG, "TX could not start");
    return false;
  }
  gpio_set_level(static_cast<gpio_num_t>(this->tx_pin_->get_pin()), step.drive_dominant);
  if (!this->arm_tx_timer_(step.delay_us)) {
    this->transmitter_.cancel();
    gpio_set_level(static_cast<gpio_num_t>(this->tx_pin_->get_pin()), 0);
    ESP_LOGE(TAG, "TX timer could not arm");
    return false;
  }
  ESP_LOGD(TAG, "TX started: type=%u payload=%s", static_cast<unsigned>(type), frame.to_string().c_str());
  return true;
#endif
}

void ScsBticinoController::loop() {
#ifdef USE_ESP32
  if (this->tx_timer_fault_) {
    this->tx_timer_fault_ = false;
    ESP_LOGE(TAG, "TX timer could not re-arm; transaction cancelled");
  }
  if (this->tx_result_ready_) {
    this->tx_result_ready_ = false;
    ESP_LOGD(TAG, "TX complete: result=%u", static_cast<unsigned>(this->tx_result_));
  }
  ScsBticinoData frame;
  if (this->receiver_.poll(&frame)) {
    ESP_LOGD(TAG, "Received: %s", frame.to_string().c_str());
  }
#endif
}

#ifdef USE_ESP32
bool ScsBticinoController::arm_tx_timer_(uint64_t alarm_us) {
  if (gptimer_stop(this->tx_timer_) != ESP_OK || gptimer_set_raw_count(this->tx_timer_, 0) != ESP_OK)
    return false;
  const gptimer_alarm_config_t config{.alarm_count = alarm_us, .flags = {.auto_reload_on_alarm = false}};
  return gptimer_set_alarm_action(this->tx_timer_, &config) == ESP_OK && gptimer_start(this->tx_timer_) == ESP_OK;
}

bool IRAM_ATTR ScsBticinoController::on_tx_timer_(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event,
                                                    void *arg) {
  auto *controller = static_cast<ScsBticinoController *>(arg);
  ScsTxStep step{};
  ScsTxResult result{};
  const bool rx_dominant = controller->transmitter_.awaiting_access()
                               ? controller->access_contended_
                                : gpio_get_level(static_cast<gpio_num_t>(controller->rx_pin_->get_pin())) == 0;
  controller->access_contended_ = false;
  const bool active = controller->transmitter_.advance(rx_dominant, &step, &result);
  if (!active) {
    gpio_set_level(static_cast<gpio_num_t>(controller->tx_pin_->get_pin()), 0);
    controller->tx_result_ = result;
    controller->tx_result_ready_ = true;
    return false;
  }
  gpio_set_level(static_cast<gpio_num_t>(controller->tx_pin_->get_pin()), step.drive_dominant);
  const gptimer_alarm_config_t config{.alarm_count = event->alarm_value + step.delay_us,
                                       .flags = {.auto_reload_on_alarm = false}};
  if (gptimer_set_alarm_action(timer, &config) != ESP_OK) {
    controller->transmitter_.cancel();
    gpio_set_level(static_cast<gpio_num_t>(controller->tx_pin_->get_pin()), 0);
    controller->tx_timer_fault_ = true;
  }
  return false;
}

void IRAM_ATTR ScsBticinoController::on_rx_edge_(void *arg) {
  auto *controller = static_cast<ScsBticinoController *>(arg);
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
