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
    this->setup_error_ = SetupError::CHANNEL;
    return false;
  }
  const rmt_rx_event_callbacks_t callbacks{.on_recv_done = &ScsBticinoReceiver::on_rx_done_};
  error = rmt_rx_register_event_callbacks(this->channel_, &callbacks, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_rx_register_event_callbacks failed: %s", esp_err_to_name(error));
    this->setup_error_ = SetupError::CALLBACK;
    return false;
  }
  error = rmt_enable(this->channel_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(error));
    this->setup_error_ = SetupError::ENABLE;
    return false;
  }
  if (!this->start_receive_()) {
    return false;
  }
  return true;
#endif
}

bool ScsBticinoReceiver::poll(ScsBticinoData *frame) {
#ifndef USE_ESP32
  return false;
#else
  if (this->receive_error_ != ESP_OK) {
    ESP_LOGE(TAG, "rmt_receive failed while re-arming: %s", esp_err_to_name(this->receive_error_));
    this->receive_error_ = ESP_OK;
    return false;
  }
  if (this->capture_overflow_) {
    ESP_LOGW(TAG, "RMT RX capture buffer overflow");
    this->capture_overflow_ = false;
  }
  if (this->capture_read_ == this->capture_write_ || frame == nullptr)
    return false;

  const uint8_t capture_index = this->capture_read_;
  const auto &capture = this->captures_[capture_index];
  const size_t symbol_count = capture.symbol_count;
  this->capture_read_ = (capture_index + 1) % RX_CAPTURE_CAPACITY;
  std::vector<ScsBticinoRun> runs;
  this->normalize_(capture.symbols, symbol_count, &runs);
  const bool decoded = ScsBticinoCodec::decode(runs, frame);
  return decoded;
#endif
}

#ifdef USE_ESP32
bool IRAM_ATTR ScsBticinoReceiver::on_rx_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event,
                                               void *arg) {
  auto *receiver = static_cast<ScsBticinoReceiver *>(arg);
  if (event->num_symbols > FILTER_SYMBOLS) {
    const uint8_t next_capture = (receiver->capture_write_ + 1) % RX_CAPTURE_CAPACITY;
    if (next_capture != receiver->capture_read_) {
      receiver->captures_[receiver->capture_write_].symbol_count = event->num_symbols;
      receiver->capture_write_ = next_capture;
    } else {
      receiver->capture_overflow_ = true;
    }
  }
  receiver->receive_error_ = rmt_receive(channel, receiver->captures_[receiver->capture_write_].symbols,
                                          sizeof(receiver->captures_[receiver->capture_write_].symbols),
                                          &receiver->receive_config_);
  receiver->bus_busy_ = false;
  return false;
}

bool ScsBticinoReceiver::start_receive_() {
  this->receive_config_.signal_range_min_ns = FILTER_US * 1000;
  this->receive_config_.signal_range_max_ns = IDLE_US * 1000;
  const esp_err_t error = rmt_receive(this->channel_, this->captures_[this->capture_write_].symbols,
                                      sizeof(this->captures_[this->capture_write_].symbols), &this->receive_config_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(error));
    if (error == ESP_ERR_INVALID_ARG)
      this->setup_error_ = SetupError::RECEIVE_INVALID_ARGUMENT;
    else if (error == ESP_ERR_INVALID_STATE)
      this->setup_error_ = SetupError::RECEIVE_INVALID_STATE;
    else if (error == ESP_ERR_NO_MEM)
      this->setup_error_ = SetupError::RECEIVE_NO_MEMORY;
    else
      this->setup_error_ = SetupError::RECEIVE_OTHER;
    return false;
  }
  return true;
}

void ScsBticinoReceiver::normalize_(const rmt_symbol_word_t *symbols, size_t symbol_count,
                                    std::vector<ScsBticinoRun> *runs) const {
  runs->clear();
  runs->reserve(symbol_count * 2 + 1);

  bool previous_level = false;
  bool idle_level = false;
  uint32_t previous_length = 0;
  const auto append = [runs](bool released, uint32_t duration_us) {
    if (duration_us == 0)
      return;
    if (!runs->empty() && runs->back().released == released)
      runs->back().duration_us += duration_us;
    else
      runs->push_back({released, duration_us});
  };
  const auto consume = [&](bool level, uint32_t duration) {
    if (duration == 0)
      return false;
    if (level == previous_level || duration < FILTER_US) {
      previous_length += duration;
    } else {
      if (previous_length >= FILTER_US)
        append(previous_level, previous_length);
      previous_level = level;
      previous_length = duration;
    }
    idle_level = !level;
    return true;
  };

  for (size_t index = 0; index < symbol_count; index++) {
    const auto &symbol = symbols[index];
    if (!consume(symbol.level0 != 0, symbol.duration0) || !consume(symbol.level1 != 0, symbol.duration1))
      break;
  }
  if (previous_length >= FILTER_US && previous_level != idle_level)
    append(previous_level, previous_length);
  if (!runs->empty())
    append(idle_level, IDLE_US);
}
#endif

}  // namespace esphome::scs_bticino
