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

  rmt_rx_channel_config_t rx_config{};
  rx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rx_config.resolution_hz = 1000000;
  rx_config.mem_block_symbols = RX_SYMBOL_CAPACITY;
  rx_config.gpio_num = static_cast<gpio_num_t>(this->rx_pin_->get_pin());
  rx_config.flags.invert_in = false;
  rx_config.flags.with_dma = false;
  esp_err_t error = rmt_new_rx_channel(&rx_config, &this->rx_channel_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_rx_channel failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }

  const rmt_rx_event_callbacks_t rx_callbacks{.on_recv_done = &ScsBticinoController::on_rx_done_};
  error = rmt_rx_register_event_callbacks(this->rx_channel_, &rx_callbacks, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_rx_register_event_callbacks failed: %s", esp_err_to_name(error));
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
  error = rmt_new_tx_channel(&tx_config, &this->tx_channel_);
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

  error = rmt_enable(this->rx_channel_);
  if (error == ESP_OK)
    error = rmt_enable(this->tx_channel_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }
  if (!this->start_receive_())
    this->mark_failed();
#endif
}

void ScsBticinoController::loop() {
#ifdef USE_ESP32
  if (this->rx_done_) {
    this->rx_done_ = false;
    this->handle_received_();
    if (!this->start_receive_())
      this->mark_failed();
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

#ifdef USE_ESP32
bool IRAM_ATTR ScsBticinoController::on_rx_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event,
                                                  void *arg) {
  auto *controller = static_cast<ScsBticinoController *>(arg);
  controller->rx_symbol_count_ = event->num_symbols;
  controller->rx_done_ = true;
  return false;
}

bool ScsBticinoController::start_receive_() {
  const rmt_receive_config_t receive_config{
      .signal_range_min_ns = 10000,
      .signal_range_max_ns = 2000000,
  };
  const esp_err_t error = rmt_receive(this->rx_channel_, this->rx_symbols_, sizeof(this->rx_symbols_), &receive_config);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(error));
    return false;
  }
  return true;
}

void ScsBticinoController::handle_received_() {
  std::vector<ScsBticinoRun> runs;
  runs.reserve(this->rx_symbol_count_ * 2);
  for (size_t index = 0; index < this->rx_symbol_count_; index++) {
    const auto &symbol = this->rx_symbols_[index];
    const auto append = [&runs](bool released, uint32_t duration_us) {
      if (duration_us == 0)
        return;
      if (!runs.empty() && runs.back().released == released) {
        runs.back().duration_us += duration_us;
      } else {
        runs.push_back({released, duration_us});
      }
    };
    append(symbol.level0 == 0, symbol.duration0);
    append(symbol.level1 == 0, symbol.duration1);
  }

  ScsBticinoData frame;
  if (ScsBticinoCodec::decode(runs, &frame)) {
    ESP_LOGD(TAG, "Received: %s", frame.to_string().c_str());
  }
}
#endif

}  // namespace esphome::scs_bticino
