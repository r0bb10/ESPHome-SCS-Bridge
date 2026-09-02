#include "scsbticino.h"

#include "scsbticino_codec.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <vector>

#include "esp_err.h"
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

  rmt_tx_channel_config_t tx_config{};
  tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_config.resolution_hz = 1000000;
  tx_config.mem_block_symbols = 64;
  tx_config.trans_queue_depth = 1;
  tx_config.gpio_num = static_cast<gpio_num_t>(this->tx_pin_->get_pin());
  tx_config.flags.invert_out = false;
  tx_config.flags.with_dma = false;
  tx_config.flags.io_loop_back = false;
  tx_config.flags.io_od_mode = false;
  esp_err_t error = rmt_new_tx_channel(&tx_config, &this->tx_channel_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }

  const rmt_copy_encoder_config_t encoder_config{};
  error = rmt_new_copy_encoder(&encoder_config, &this->tx_encoder_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }

  error = rmt_enable(this->tx_channel_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }
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

void ScsBticinoController::dump_config() {
  ESP_LOGCONFIG(TAG, "SCS Bticino:");
  LOG_PIN("  RX Pin: ", this->rx_pin_);
  LOG_PIN("  TX Pin: ", this->tx_pin_);
  ESP_LOGCONFIG(TAG, "  RMT resolution: 1 MHz");
  ESP_LOGCONFIG(TAG, "  TX polarity/released level: normal/low (F422 interface)");
}

}  // namespace esphome::scs_bticino
