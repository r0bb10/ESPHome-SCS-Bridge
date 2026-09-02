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
    this->mark_failed();
    return;
  }

  if (!this->receiver_.setup(this->rx_pin_->get_pin())) {
    ESP_LOGE(TAG, "Unable to initialize RMT RX");
    this->mark_failed();
    return;
  }

  this->tx_pin_->setup();
  this->tx_pin_->digital_write(false);
  gptimer_config_t timer_config{};
  timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  timer_config.direction = GPTIMER_COUNT_UP;
  timer_config.resolution_hz = 1000000;
  esp_err_t error = gptimer_new_timer(&timer_config, &this->tx_timer_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "gptimer_new_timer failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }
  const gptimer_event_callbacks_t timer_callbacks{.on_alarm = &ScsBticinoController::on_tx_timer_};
  error = gptimer_register_event_callbacks(this->tx_timer_, &timer_callbacks, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "gptimer_register_event_callbacks failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }
  error = gptimer_enable(this->tx_timer_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "gptimer_enable failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }
  const auto rx_gpio = static_cast<gpio_num_t>(this->rx_pin_->get_pin());
  gpio_set_intr_type(rx_gpio, GPIO_INTR_POSEDGE);
  error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }
  error = gpio_isr_handler_add(rx_gpio, &ScsBticinoController::on_rx_edge_, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(error));
    this->mark_failed();
  }
#endif
}

bool ScsBticinoController::send(const ScsBticinoData &frame, ScsTxType type) {
#ifndef USE_ESP32
  return false;
#else
  if (!this->transmitter_.enqueue(frame, type))
    return false;
  ScsTxStep step{};
  ScsTxResult result{};
  if (!this->transmitter_.advance(false, &step, &result))
    return false;
  this->tx_pin_->digital_write(step.drive_dominant);
  this->next_alarm_us_ = step.delay_us;
  return this->arm_tx_timer_(this->next_alarm_us_);
#endif
}

void ScsBticinoController::loop() {
#ifdef USE_ESP32
  ScsBticinoData frame;
  if (this->receiver_.poll(&frame)) {
    ESP_LOGD(TAG, "Received: %s", frame.to_string().c_str());
  }
#endif
}

#ifdef USE_ESP32
bool ScsBticinoController::arm_tx_timer_(uint64_t alarm_us) {
  const gptimer_alarm_config_t config{.alarm_count = alarm_us, .flags = {.auto_reload_on_alarm = false}};
  return gptimer_set_alarm_action(this->tx_timer_, &config) == ESP_OK && gptimer_start(this->tx_timer_) == ESP_OK;
}

bool IRAM_ATTR ScsBticinoController::on_tx_timer_(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event,
                                                   void *arg) {
  auto *controller = static_cast<ScsBticinoController *>(arg);
  ScsTxStep step{};
  ScsTxResult result{};
  const bool active = controller->transmitter_.advance(controller->bus_dominant_, &step, &result);
  controller->bus_dominant_ = false;
  if (!active) {
    controller->tx_pin_->digital_write(false);
    controller->tx_result_ = result;
    controller->tx_result_ready_ = true;
    return false;
  }
  controller->tx_pin_->digital_write(step.drive_dominant);
  const gptimer_alarm_config_t config{.alarm_count = event->alarm_value + step.delay_us,
                                      .flags = {.auto_reload_on_alarm = false}};
  gptimer_set_alarm_action(timer, &config);
  return false;
}

void IRAM_ATTR ScsBticinoController::on_rx_edge_(void *arg) {
  static_cast<ScsBticinoController *>(arg)->bus_dominant_ = true;
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
