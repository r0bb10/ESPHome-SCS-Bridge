#include "scs_rmt.h"

namespace esphome::scs_bus {

ScsRmt::~ScsRmt() { this->teardown(); }

esp_err_t ScsRmt::setup(int rx_pin, int tx_pin, bool rx_inverted, bool tx_inverted) {
  if (this->configured_)
    return ESP_ERR_INVALID_STATE;

  rmt_rx_channel_config_t rx_config{};
  rx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rx_config.resolution_hz = RESOLUTION_HZ;
  rx_config.mem_block_symbols = RX_SYMBOLS;
  rx_config.gpio_num = static_cast<gpio_num_t>(rx_pin);
  rx_config.flags.invert_in = rx_inverted;
  rx_config.flags.with_dma = false;
  esp_err_t err = rmt_new_rx_channel(&rx_config, &this->rx_channel_);
  if (err != ESP_OK)
    return err;

  rmt_tx_channel_config_t tx_config{};
  tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_config.resolution_hz = RESOLUTION_HZ;
  tx_config.mem_block_symbols = MAX_TX_BYTES * MAX_SYMBOLS_PER_BYTE;
  tx_config.trans_queue_depth = 1;
  tx_config.gpio_num = static_cast<gpio_num_t>(tx_pin);
  tx_config.flags.invert_out = tx_inverted;
  tx_config.flags.with_dma = false;
  err = rmt_new_tx_channel(&tx_config, &this->tx_channel_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  rmt_copy_encoder_config_t copy_config = {};
  err = rmt_new_copy_encoder(&copy_config, &this->copy_encoder_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  rmt_rx_event_callbacks_t rx_callbacks = {.on_recv_done = &ScsRmt::on_receive_done_};
  err = rmt_rx_register_event_callbacks(this->rx_channel_, &rx_callbacks, this);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }
  rmt_tx_event_callbacks_t tx_callbacks = {.on_trans_done = &ScsRmt::on_transmit_done_};
  err = rmt_tx_register_event_callbacks(this->tx_channel_, &tx_callbacks, this);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  err = rmt_enable(this->rx_channel_);
  if (err == ESP_OK)
    err = rmt_enable(this->tx_channel_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  this->configured_ = true;
  err = this->start_receive();
  if (err != ESP_OK)
    this->teardown();
  return err;
}

esp_err_t ScsRmt::start_receive() {
  if (!this->configured_)
    return ESP_ERR_INVALID_STATE;

  rmt_receive_config_t config = {
      .signal_range_min_ns = 10000,
      // SCS cells contain runs up to 105 us. A 2 ms idle gap ends a capture.
      .signal_range_max_ns = 2000000,
  };
  this->receive_complete_ = false;
  return rmt_receive(this->rx_channel_, this->rx_symbols_, sizeof(this->rx_symbols_), &config);
}

esp_err_t ScsRmt::transmit(const uint8_t *data, size_t length) {
  if (!this->configured_ || data == nullptr || length == 0 || length > MAX_TX_BYTES)
    return ESP_ERR_INVALID_ARG;
  if (this->transmitting_)
    return ESP_ERR_INVALID_STATE;

  size_t symbol_count = 0;
  for (size_t byte_index = 0; byte_index < length; byte_index++) {
    const size_t encoded = this->encode_byte_(data[byte_index], &this->tx_symbols_[symbol_count],
                                               MAX_TX_BYTES * MAX_SYMBOLS_PER_BYTE - symbol_count);
    if (encoded == 0)
      return ESP_ERR_INVALID_SIZE;
    symbol_count += encoded;
  }

  rmt_transmit_config_t config = {
      .loop_count = 0,
      .flags = {.eot_level = 1},
  };
  this->transmitting_ = true;
  esp_err_t err = rmt_transmit(this->tx_channel_, this->copy_encoder_, this->tx_symbols_,
                                symbol_count * sizeof(rmt_symbol_word_t), &config);
  if (err != ESP_OK)
    this->transmitting_ = false;
  return err;
}

void ScsRmt::loop() {
  if (this->configured_ && this->receive_complete_) {
    this->receive_complete_ = false;
    this->decode_(this->rx_symbols_, this->received_symbol_count_);
    this->start_receive();
  }
  while (this->received_tail_ != this->received_head_) {
    const uint8_t byte = this->received_bytes_[this->received_tail_];
    this->received_tail_ = (this->received_tail_ + 1) % RX_QUEUE_SIZE;
    if (this->receive_callback_ != nullptr)
      this->receive_callback_(this->receive_context_, byte);
  }
  if (this->transmit_complete_) {
    this->transmit_complete_ = false;
    if (this->transmit_done_callback_ != nullptr)
      this->transmit_done_callback_(this->transmit_done_context_);
  }
}

void ScsRmt::teardown() {
  this->configured_ = false;
  this->receive_complete_ = false;
  this->transmit_complete_ = false;
  this->transmitting_ = false;
  if (this->copy_encoder_ != nullptr) {
    rmt_del_encoder(this->copy_encoder_);
    this->copy_encoder_ = nullptr;
  }
  if (this->rx_channel_ != nullptr) {
    rmt_disable(this->rx_channel_);
    rmt_del_channel(this->rx_channel_);
    this->rx_channel_ = nullptr;
  }
  if (this->tx_channel_ != nullptr) {
    rmt_disable(this->tx_channel_);
    rmt_del_channel(this->tx_channel_);
    this->tx_channel_ = nullptr;
  }
}

bool ScsRmt::on_receive_done_(rmt_channel_handle_t, const rmt_rx_done_event_data_t *event, void *context) {
  auto *transport = static_cast<ScsRmt *>(context);
  transport->received_symbol_count_ = event->num_symbols;
  transport->receive_complete_ = true;
  return false;
}

bool ScsRmt::on_transmit_done_(rmt_channel_handle_t, const rmt_tx_done_event_data_t *, void *context) {
  auto *transport = static_cast<ScsRmt *>(context);
  transport->transmitting_ = false;
  transport->transmit_complete_ = true;
  return false;
}

void ScsRmt::decode_(const rmt_symbol_word_t *symbols, size_t symbol_count) {
  // The RMT combines equal-level boundaries, so sample the continuous waveform
  // instead of assuming that one received RMT symbol equals one SCS bit cell.
  uint32_t cell_start = 0;
  const uint32_t duration = [&]() {
    uint32_t total = 0;
    for (size_t i = 0; i < symbol_count; i++)
      total += symbols[i].duration0 + symbols[i].duration1;
    return total;
  }();

  while (cell_start + CELL_US * 10 <= duration) {
    uint8_t start_level;
    uint8_t stop_level;
    if (!this->sample_level_(symbols, symbol_count, cell_start + SAMPLE_US, &start_level) || start_level != 0 ||
        !this->sample_level_(symbols, symbol_count, cell_start + CELL_US * 9 + SAMPLE_US, &stop_level) || stop_level != 1) {
      cell_start += CELL_US;
      continue;
    }

    uint8_t value = 0;
    bool valid = true;
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint8_t level;
      if (!this->sample_level_(symbols, symbol_count, cell_start + CELL_US * (bit + 1) + SAMPLE_US, &level)) {
        valid = false;
        break;
      }
      value |= level << bit;
    }
    if (valid) {
      const uint8_t next_head = (this->received_head_ + 1) % RX_QUEUE_SIZE;
      if (next_head != this->received_tail_) {
        this->received_bytes_[this->received_head_] = value;
        this->received_head_ = next_head;
      }
    }
    cell_start += CELL_US * 10;
  }
}

bool ScsRmt::sample_level_(const rmt_symbol_word_t *symbols, size_t symbol_count, uint32_t time_us, uint8_t *level) const {
  uint32_t elapsed = 0;
  for (size_t i = 0; i < symbol_count; i++) {
    if (time_us < elapsed + symbols[i].duration0) {
      *level = symbols[i].level0;
      return true;
    }
    elapsed += symbols[i].duration0;
    if (time_us < elapsed + symbols[i].duration1) {
      *level = symbols[i].level1;
      return true;
    }
    elapsed += symbols[i].duration1;
  }
  return false;
}

size_t ScsRmt::encode_byte_(uint8_t byte, rmt_symbol_word_t *symbols, size_t symbol_capacity) const {
  struct Run {
    bool level;
    uint16_t duration_us;
  };
  Run runs[19]{};
  size_t run_count = 0;
  const auto append_run = [&runs, &run_count](bool level, uint16_t duration_us) {
    if (run_count > 0 && runs[run_count - 1].level == level) {
      runs[run_count - 1].duration_us += duration_us;
      return;
    }
    runs[run_count++] = {level, duration_us};
  };

  append_run(false, HALF_CELL_US);  // Start pulse.
  append_run(true, CELL_US - HALF_CELL_US);

  for (uint8_t bit = 0; bit < 8; bit++) {
    if ((byte & (1U << bit)) == 0) {
      append_run(false, HALF_CELL_US);
      append_run(true, CELL_US - HALF_CELL_US);
    } else {
      append_run(true, CELL_US);
    }
  }

  append_run(true, CELL_US);  // Stop bit.
  const size_t symbol_count = (run_count + 1) / 2;
  if (symbol_count > symbol_capacity)
    return 0;
  for (size_t index = 0; index < symbol_count; index++) {
    const Run &first = runs[index * 2];
    const Run second = index * 2 + 1 < run_count ? runs[index * 2 + 1] : Run{first.level, 1};
    symbols[index].level0 = first.level;
    symbols[index].duration0 = first.duration_us;
    symbols[index].level1 = second.level;
    symbols[index].duration1 = second.duration_us;
  }
  return symbol_count;
}

}  // namespace esphome::scs_bus
