#pragma once

#include "scsbticino_codec.h"

#ifdef USE_ESP32
#include "driver/rmt_rx.h"
#endif

namespace esphome::scs_bticino {

class ScsBticinoReceiver {
 public:
  bool setup(int gpio_num);
  bool poll(ScsBticinoData *frame);

 protected:
#ifdef USE_ESP32
  static bool on_rx_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *arg);
  bool start_receive_();

  static constexpr size_t RX_SYMBOL_CAPACITY = 128;
  rmt_channel_handle_t channel_{nullptr};
  rmt_symbol_word_t symbols_[RX_SYMBOL_CAPACITY]{};
  volatile size_t symbol_count_{0};
  volatile bool done_{false};
#endif
};

}  // namespace esphome::scs_bticino
