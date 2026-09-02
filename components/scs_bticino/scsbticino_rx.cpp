#include "scsbticino_rx.h"

#ifdef USE_ESP32
#include <vector>

#include "esp_err.h"
#include "esphome/core/log.h"
#endif

namespace esphome::scs_bticino {

static const char *const TAG = "scs_bticino.rx";

bool ScsBticinoReceiver::setup(int gpio_num) {
#ifndef USE_ESP32
  return false;
#else
  rmt_rx_channel_config_t config{};
  config.clk_src = RMT_CLK_SRC_DEFAULT;
  config.resolution_hz = 1000000;
  config.mem_block_symbols = RX_SYMBOL_CAPACITY;
  config.gpio_num = static_cast<gpio_num_t>(gpio_num);
  config.flags.invert_in = false;
  config.flags.with_dma = false;
  esp_err_t error = rmt_new_rx_channel(&config, &this->channel_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_rx_channel failed: %s", esp_err_to_name(error));
    return false;
  }
  const rmt_rx_event_callbacks_t callbacks{.on_recv_done = &ScsBticinoReceiver::on_rx_done_};
  error = rmt_rx_register_event_callbacks(this->channel_, &callbacks, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_rx_register_event_callbacks failed: %s", esp_err_to_name(error));
    return false;
  }
  error = rmt_enable(this->channel_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(error));
    return false;
  }
  return this->start_receive_();
#endif
}

bool ScsBticinoReceiver::poll(ScsBticinoData *frame) {
#ifndef USE_ESP32
  return false;
#else
  if (!this->done_ || frame == nullptr)
    return false;
  this->done_ = false;
  std::vector<ScsBticinoRun> runs;
  runs.reserve(this->symbol_count_ * 2);
  for (size_t index = 0; index < this->symbol_count_; index++) {
    const auto &symbol = this->symbols_[index];
    const auto append = [&runs](bool released, uint32_t duration_us) {
      if (duration_us == 0)
        return;
      if (!runs.empty() && runs.back().released == released)
        runs.back().duration_us += duration_us;
      else
        runs.push_back({released, duration_us});
    };
    append(symbol.level0 == 0, symbol.duration0);
    append(symbol.level1 == 0, symbol.duration1);
  }
  const bool decoded = ScsBticinoCodec::decode(runs, frame);
  if (!this->start_receive_()) {
    ESP_LOGE(TAG, "Unable to re-arm RMT RX");
  }
  return decoded;
#endif
}

#ifdef USE_ESP32
bool IRAM_ATTR ScsBticinoReceiver::on_rx_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event,
                                               void *arg) {
  auto *receiver = static_cast<ScsBticinoReceiver *>(arg);
  receiver->symbol_count_ = event->num_symbols;
  receiver->done_ = true;
  return false;
}

bool ScsBticinoReceiver::start_receive_() {
  const rmt_receive_config_t config{
      .signal_range_min_ns = 10000,
      .signal_range_max_ns = 2000000,
  };
  const esp_err_t error = rmt_receive(this->channel_, this->symbols_, sizeof(this->symbols_), &config);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(error));
    return false;
  }
  return true;
}
#endif

}  // namespace esphome::scs_bticino
