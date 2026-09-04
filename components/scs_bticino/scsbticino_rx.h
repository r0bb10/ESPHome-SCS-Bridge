#pragma once

#include <array>
#include <cstddef>

#include "scsbticino_codec.h"

#ifdef USE_ESP32
#include "esp_attr.h"
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
  bool bus_busy();
#ifdef USE_ESP32
  void IRAM_ATTR on_bus_edge() {
    this->bus_busy_ = true;
    this->bus_edge_count_++;
  }
#endif
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
  static constexpr uint32_t BUS_BUSY_HOLD_MS = 2;
  static constexpr size_t FILTER_SYMBOLS = 0;
  struct Capture {
    size_t symbol_count{0};
    alignas(rmt_symbol_word_t) rmt_symbol_word_t symbols[RX_SYMBOL_CAPACITY]{};
  };
  static constexpr size_t RX_CAPTURE_CAPACITY = RX_BUFFER_BYTES / sizeof(Capture);
  static_assert(RX_CAPTURE_CAPACITY > 1, "RX capture buffer needs a producer and consumer slot");
  rmt_channel_handle_t channel_{nullptr};
  rmt_receive_config_t receive_config_{};
  // The ESP32-C3 validation target uses this as a single-producer (RMT ISR),
  // single-consumer (ESPHome loop) ring. A dual-core target needs explicit
  // ISR/task synchronization around publication and consumption of indices.
  std::array<Capture, RX_CAPTURE_CAPACITY> captures_{};
  volatile uint8_t capture_read_{0};
  volatile uint8_t capture_write_{0};
  volatile bool capture_overflow_{false};
  volatile uint16_t capture_dropped_{0};
  volatile bool bus_busy_{false};
  volatile uint32_t bus_edge_count_{0};
  uint32_t observed_bus_edge_count_{0};
  uint32_t bus_busy_until_ms_{0};
  volatile esp_err_t receive_error_{ESP_OK};
#endif
};

}  // namespace esphome::scs_bticino
