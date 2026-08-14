#include "scs_transport.h"

#include "esp_timer.h"
#include "esp32c3/rom/gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"

namespace esphome::scs_bus {

ScsTransport::~ScsTransport() { this->teardown(); }

esp_err_t ScsTransport::setup(int rx_pin, int tx_pin, bool rx_inverted, bool tx_inverted) {
  if (this->configured_)
    return ESP_ERR_INVALID_STATE;

  this->rx_pin_ = rx_pin;
  this->rx_inverted_ = rx_inverted;
  this->tx_pin_ = tx_pin;
  this->tx_inverted_ = tx_inverted;
  gpio_config_t rx_config{};
  rx_config.pin_bit_mask = 1ULL << rx_pin;
  rx_config.mode = GPIO_MODE_INPUT;
  rx_config.intr_type = GPIO_INTR_ANYEDGE;
  esp_err_t err = gpio_config(&rx_config);
  if (err != ESP_OK)
    return err;

  err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    return err;

  gptimer_config_t timer_config{};
  timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  timer_config.direction = GPTIMER_COUNT_UP;
  timer_config.resolution_hz = RESOLUTION_HZ;
  err = gptimer_new_timer(&timer_config, &this->rx_timer_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }
  uint32_t timer_resolution_hz = 0;
  err = gptimer_get_resolution(this->rx_timer_, &timer_resolution_hz);
  if (err != ESP_OK || timer_resolution_hz != RESOLUTION_HZ) {
    this->teardown();
    return ESP_ERR_NOT_SUPPORTED;
  }
  gptimer_event_callbacks_t timer_callbacks = {.on_alarm = &ScsTransport::on_rx_timer_};
  err = gptimer_register_event_callbacks(this->rx_timer_, &timer_callbacks, this);
  if (err == ESP_OK)
    err = gptimer_enable(this->rx_timer_);
  if (err == ESP_OK)
    err = gptimer_start(this->rx_timer_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }
  err = gpio_isr_handler_add(static_cast<gpio_num_t>(rx_pin), &ScsTransport::on_rx_edge_, this);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  gptimer_config_t wake_timer_config{};
  wake_timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  wake_timer_config.direction = GPTIMER_COUNT_UP;
  wake_timer_config.resolution_hz = RESOLUTION_HZ;
  err = gptimer_new_timer(&wake_timer_config, &this->task_wake_timer_);
  if (err == ESP_OK) {
    uint32_t wake_timer_resolution_hz = 0;
    err = gptimer_get_resolution(this->task_wake_timer_, &wake_timer_resolution_hz);
    if (err == ESP_OK && wake_timer_resolution_hz != RESOLUTION_HZ)
      err = ESP_ERR_NOT_SUPPORTED;
  }
  if (err == ESP_OK) {
    gptimer_event_callbacks_t wake_timer_callbacks = {.on_alarm = &ScsTransport::on_task_wake_timer_};
    err = gptimer_register_event_callbacks(this->task_wake_timer_, &wake_timer_callbacks, this);
  }
  if (err == ESP_OK)
    err = gptimer_enable(this->task_wake_timer_);
  if (err == ESP_OK)
    err = gptimer_start(this->task_wake_timer_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  gpio_config_t tx_gpio_config{};
  tx_gpio_config.pin_bit_mask = 1ULL << tx_pin;
  tx_gpio_config.mode = GPIO_MODE_OUTPUT;
  err = gpio_config(&tx_gpio_config);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }
  gpio_set_level(static_cast<gpio_num_t>(tx_pin), tx_inverted ? 0 : 1);

  rmt_tx_channel_config_t tx_config{};
  tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_config.resolution_hz = RESOLUTION_HZ;
  tx_config.mem_block_symbols = RMT_CHANNEL_SYMBOLS;
  tx_config.trans_queue_depth = 1;
  tx_config.gpio_num = static_cast<gpio_num_t>(tx_pin);
  tx_config.flags.invert_out = tx_inverted;
  tx_config.flags.with_dma = false;
  err = rmt_new_tx_channel(&tx_config, &this->tx_channel_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  rmt_copy_encoder_config_t copy_config{};
  err = rmt_new_copy_encoder(&copy_config, &this->copy_encoder_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  rmt_tx_event_callbacks_t tx_callbacks = {.on_trans_done = &ScsTransport::on_transmit_done_};
  err = rmt_tx_register_event_callbacks(this->tx_channel_, &tx_callbacks, this);
  if (err == ESP_OK)
    err = rmt_enable(this->tx_channel_);
  if (err != ESP_OK) {
    this->teardown();
    return err;
  }

  this->configured_ = true;
  return ESP_OK;
}

esp_err_t ScsTransport::transmit(const uint8_t *data, size_t length, uint32_t transaction_id) {
  if (!this->configured_ || data == nullptr || length == 0 || length > MAX_TX_BYTES)
    return ESP_ERR_INVALID_ARG;
  if (this->transmitting_)
    return ESP_ERR_INVALID_STATE;

  for (size_t index = 0; index < length; index++)
    this->expected_tx_[index] = data[index];
  this->expected_tx_size_ = length;
  this->expected_tx_index_ = 0;
  this->checking_echo_ = true;
  this->echo_prefix_pending_ = false;
  this->collision_detected_ = false;
  this->transmitting_transaction_id_ = transaction_id;

  size_t symbol_count = 0;
  for (size_t byte_index = 0; byte_index < length; byte_index++) {
    ScsSerialRun runs[SCS_SERIAL_MAX_RUNS]{};
    const size_t run_count = scs_encode_serial_byte(data[byte_index], runs, SCS_SERIAL_MAX_RUNS);
    const size_t encoded = (run_count + 1) / 2;
    if (run_count == 0 || symbol_count + encoded > MAX_TX_BYTES * MAX_SYMBOLS_PER_BYTE) {
      this->checking_echo_ = false;
      this->transmitting_transaction_id_ = 0;
      return ESP_ERR_INVALID_SIZE;
    }
    for (size_t index = 0; index < encoded; index++) {
      const ScsSerialRun &first = runs[index * 2];
      const ScsSerialRun second = index * 2 + 1 < run_count ? runs[index * 2 + 1] : ScsSerialRun{first.level, 1};
      this->tx_symbols_[symbol_count + index].level0 = first.level;
      this->tx_symbols_[symbol_count + index].duration0 = first.duration_ticks;
      this->tx_symbols_[symbol_count + index].level1 = second.level;
      this->tx_symbols_[symbol_count + index].duration1 = second.duration_ticks;
    }
    symbol_count += encoded;
  }

  rmt_transmit_config_t config = {
      .loop_count = 0,
      .flags = {.eot_level = 1},
  };
  this->transmitting_ = true;
  esp_err_t err = rmt_transmit(this->tx_channel_, this->copy_encoder_, this->tx_symbols_,
                                symbol_count * sizeof(rmt_symbol_word_t), &config);
  if (err != ESP_OK) {
    this->transmitting_ = false;
    this->checking_echo_ = false;
    this->transmitting_transaction_id_ = 0;
  }
  return err;
}

esp_err_t ScsTransport::cancel_transmit() {
  if (!this->configured_)
    return ESP_ERR_INVALID_STATE;

  const bool reset_channel = this->transmitting_ || this->tx_disconnected_;
  esp_err_t err = reset_channel ? rmt_disable(this->tx_channel_) : ESP_OK;
  this->transmitting_ = false;
  this->transmit_complete_ = false;
  this->checking_echo_ = false;
  this->echo_prefix_pending_ = false;
  this->expected_tx_index_ = 0;
  this->expected_tx_size_ = 0;
  this->transmitting_transaction_id_ = 0;
  if (err != ESP_OK)
    return err;
  if (!reset_channel)
    return ESP_OK;
  if (this->tx_disconnected_) {
    err = rmt_tx_switch_gpio(this->tx_channel_, static_cast<gpio_num_t>(this->tx_pin_), this->tx_inverted_);
    if (err != ESP_OK)
      return err;
    this->tx_disconnected_ = false;
  }
  return rmt_enable(this->tx_channel_);
}

bool ScsTransport::take_rx_event(RxEvent *event) {
  uint8_t tail = this->received_tail_.load(std::memory_order_relaxed);
  if (tail == this->received_head_.load(std::memory_order_acquire))
    return false;
  *event = this->received_events_[tail];
  tail = (tail + 1) % RX_QUEUE_SIZE;
  this->received_tail_.store(tail, std::memory_order_release);
  return true;
}

bool ScsTransport::take_transmit_done(uint32_t *transaction_id, uint32_t *completed_at_us) {
  if (this->transmit_complete_) {
    this->transmit_complete_ = false;
    *transaction_id = this->completed_transaction_id_;
    *completed_at_us = this->completed_at_us_;
    return true;
  }
  return false;
}

void ScsTransport::arm_event_task_in(uint32_t delay_us) {
  if (this->task_wake_timer_ == nullptr)
    return;
  gptimer_set_raw_count(this->task_wake_timer_, 0);
  gptimer_alarm_config_t config = {
      .alarm_count = delay_us == 0 ? 1 : static_cast<uint64_t>(delay_us) * TICKS_PER_US,
      .reload_count = 0,
      .flags = {.auto_reload_on_alarm = false},
  };
  gptimer_set_alarm_action(this->task_wake_timer_, &config);
}

void ScsTransport::teardown() {
  this->configured_ = false;
  this->transmit_complete_ = false;
  this->transmitting_ = false;
  this->checking_echo_ = false;
  if (this->rx_pin_ >= 0)
    gpio_isr_handler_remove(static_cast<gpio_num_t>(this->rx_pin_));
  if (this->rx_timer_ != nullptr) {
    gptimer_stop(this->rx_timer_);
    gptimer_disable(this->rx_timer_);
    gptimer_del_timer(this->rx_timer_);
    this->rx_timer_ = nullptr;
  }
  if (this->task_wake_timer_ != nullptr) {
    gptimer_stop(this->task_wake_timer_);
    gptimer_disable(this->task_wake_timer_);
    gptimer_del_timer(this->task_wake_timer_);
    this->task_wake_timer_ = nullptr;
  }
  if (this->copy_encoder_ != nullptr) {
    rmt_del_encoder(this->copy_encoder_);
    this->copy_encoder_ = nullptr;
  }
  if (this->tx_channel_ != nullptr) {
    rmt_disable(this->tx_channel_);
    rmt_del_channel(this->tx_channel_);
    this->tx_channel_ = nullptr;
  }
}

bool ScsTransport::on_transmit_done_(rmt_channel_handle_t, const rmt_tx_done_event_data_t *, void *context) {
  auto *transport = static_cast<ScsTransport *>(context);
  transport->last_bus_activity_us_ = static_cast<uint32_t>(esp_timer_get_time());
  if (!transport->transmitting_)
    return false;
  transport->transmitting_ = false;
  transport->completed_transaction_id_ = transport->transmitting_transaction_id_;
  transport->completed_at_us_ = static_cast<uint32_t>(esp_timer_get_time());
  transport->transmitting_transaction_id_ = 0;
  transport->transmit_complete_ = true;
  if (transport->event_task_ != nullptr) {
    BaseType_t awakened = pdFALSE;
    vTaskNotifyGiveFromISR(transport->event_task_, &awakened);
    if (awakened)
      portYIELD_FROM_ISR();
  }
  return false;
}

bool IRAM_ATTR ScsTransport::on_task_wake_timer_(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *context) {
  auto *transport = static_cast<ScsTransport *>(context);
  if (transport->event_task_ == nullptr)
    return false;
  BaseType_t awakened = pdFALSE;
  vTaskNotifyGiveFromISR(transport->event_task_, &awakened);
  return awakened == pdTRUE;
}

void IRAM_ATTR ScsTransport::on_rx_edge_(void *context) {
  auto *transport = static_cast<ScsTransport *>(context);
  transport->last_bus_activity_us_ = static_cast<uint32_t>(esp_timer_get_time());
  const uint8_t level = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(transport->rx_pin_))) ^
                        transport->rx_inverted_;
  if (level != 0 || !transport->receiver_.start())
    return;

  gptimer_set_raw_count(transport->rx_timer_, 0);
  transport->schedule_rx_sample_(transport->receiver_.next_sample_delay_ticks());
}

bool IRAM_ATTR ScsTransport::on_rx_timer_(gptimer_handle_t, const gptimer_alarm_event_data_t *event, void *context) {
  auto *transport = static_cast<ScsTransport *>(context);
  const uint8_t level = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(transport->rx_pin_))) ^
                        transport->rx_inverted_;
  if (transport->receiver_.state() == ScsSerialReceiver::State::DATA) {
      if (transport->checking_echo_ && transport->expected_tx_index_ < transport->expected_tx_size_ &&
          level != ((transport->expected_tx_[transport->expected_tx_index_] >> transport->receiver_.bit_index()) & 1U)) {
        transport->collision_detected_ = true;
        transport->abort_transmit_from_isr_();
      }
  }
  uint8_t byte = 0;
  const ScsSerialReceiver::SampleResult result = transport->receiver_.sample(level, &byte);
  if (result == ScsSerialReceiver::SampleResult::BYTE)
    transport->queue_rx_byte_(byte);
  else if (result == ScsSerialReceiver::SampleResult::INVALID) {
    if (transport->checking_echo_) {
      transport->collision_detected_ = true;
      transport->abort_transmit_from_isr_();
    }
    transport->flush_echo_prefix_();
  }
  if (transport->receiver_.receiving())
    transport->schedule_rx_sample_(event->alarm_value + transport->receiver_.next_sample_delay_ticks());
  return false;
}

void IRAM_ATTR ScsTransport::schedule_rx_sample_(uint64_t alarm_count) {
  gptimer_alarm_config_t config = {
      .alarm_count = alarm_count,
      .reload_count = 0,
      .flags = {.auto_reload_on_alarm = false},
  };
  gptimer_set_alarm_action(this->rx_timer_, &config);
}

void IRAM_ATTR ScsTransport::queue_rx_byte_(uint8_t byte) {
  const uint32_t timestamp_us = static_cast<uint32_t>(esp_timer_get_time());
  RxEvent event{byte, timestamp_us, this->rx_sequence_.fetch_add(1, std::memory_order_relaxed) + 1};
  bool local_echo = false;
  if (this->checking_echo_) {
    if (this->expected_tx_index_ >= this->expected_tx_size_ || byte != this->expected_tx_[this->expected_tx_index_]) {
      this->collision_detected_ = true;
      this->abort_transmit_from_isr_();
    } else {
      local_echo = true;
      this->echo_prefix_[this->expected_tx_index_] = event;
      this->expected_tx_index_++;
      if (this->expected_tx_index_ == this->expected_tx_size_)
        this->checking_echo_ = false;
    }
  }
  if (local_echo)
    return;

  // Echo is speculative until the whole local transmission matches. A
  // collision turns its validated prefix into ordinary RX events exactly once.
  this->flush_echo_prefix_();
  this->queue_rx_event_(event);
}

bool IRAM_ATTR ScsTransport::queue_rx_event_(const RxEvent &event) {
  const uint8_t head = this->received_head_.load(std::memory_order_relaxed);
  const uint8_t next_head = (head + 1) % RX_QUEUE_SIZE;
  if (next_head == this->received_tail_.load(std::memory_order_acquire))
    return false;
  this->received_events_[head] = event;
  this->received_head_.store(next_head, std::memory_order_release);
  if (this->event_task_ != nullptr) {
    BaseType_t awakened = pdFALSE;
    vTaskNotifyGiveFromISR(this->event_task_, &awakened);
    if (awakened)
      portYIELD_FROM_ISR();
  }
  return true;
}

void IRAM_ATTR ScsTransport::flush_echo_prefix_() {
  if (!this->echo_prefix_pending_)
    return;
  for (uint8_t index = 0; index < this->expected_tx_index_; index++)
    this->queue_rx_event_(this->echo_prefix_[index]);
  this->expected_tx_index_ = 0;
  this->echo_prefix_pending_ = false;
}

void IRAM_ATTR ScsTransport::abort_transmit_from_isr_() {
  if (!this->transmitting_)
    return;
  gpio_matrix_out(this->tx_pin_, SIG_GPIO_OUT_IDX, false, false);
  gpio_ll_set_level(&GPIO, this->tx_pin_, this->tx_inverted_ ? 0 : 1);
  this->tx_disconnected_ = true;
  this->transmitting_ = false;
  this->transmit_complete_ = false;
  this->checking_echo_ = false;
  this->echo_prefix_pending_ = true;
}

bool ScsTransport::take_collision() {
  const bool collision = this->collision_detected_;
  this->collision_detected_ = false;
  return collision;
}

}  // namespace esphome::scs_bus
