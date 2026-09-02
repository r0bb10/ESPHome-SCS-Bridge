#pragma once

#include <array>
#include <cstddef>

#include "scsbticino_codec.h"

#ifdef USE_ESP32
#include "driver/rmt_rx.h"
#endif

namespace esphome::scs_bticino {

class ScsBticinoReceiver {
 public:
  enum class SetupError : uint8_t {
    NONE,
    CHANNEL,
    CALLBACK,
    ENABLE,
    RECEIVE_INVALID_ARGUMENT,
    RECEIVE_INVALID_STATE,
    RECEIVE_NO_MEMORY,
    RECEIVE_OTHER,
  };

  bool setup(int gpio_num);
  bool poll(ScsBticinoData *frame);
  SetupError setup_error() const { return this->setup_error_; }

 protected:
  SetupError setup_error_{SetupError::NONE};

#ifdef USE_ESP32
  static bool on_rx_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *arg);
  bool start_receive_();
  void normalize_(const rmt_symbol_word_t *symbols, size_t symbol_count, std::vector<ScsBticinoRun> *runs) const;

  // Match ESPHome's stock ESP32-C3 RMT receiver capacity. Extended SCS
  // telegrams require both available 48-symbol blocks.
  static constexpr size_t RX_SYMBOL_CAPACITY = 96;
  // Match ESPHome's stock 10 KB default receive buffer instead of dropping
  // completed captures while the application loop is busy.
  static constexpr size_t RX_BUFFER_BYTES = 10000;
  static constexpr uint32_t FILTER_US = 3;
  static constexpr uint32_t IDLE_US = 1100;
  static constexpr size_t FILTER_SYMBOLS = 0;
  struct Capture {
    size_t symbol_count{0};
    alignas(rmt_symbol_word_t) rmt_symbol_word_t symbols[RX_SYMBOL_CAPACITY]{};
  };
  static constexpr size_t RX_CAPTURE_CAPACITY = RX_BUFFER_BYTES / sizeof(Capture);
  static_assert(RX_CAPTURE_CAPACITY > 1, "RX capture buffer needs a producer and consumer slot");
  rmt_channel_handle_t channel_{nullptr};
  rmt_receive_config_t receive_config_{};
  std::array<Capture, RX_CAPTURE_CAPACITY> captures_{};
  volatile uint8_t capture_read_{0};
  volatile uint8_t capture_write_{0};
  volatile bool capture_overflow_{false};
  volatile esp_err_t receive_error_{ESP_OK};
#endif
};

}  // namespace esphome::scs_bticino
