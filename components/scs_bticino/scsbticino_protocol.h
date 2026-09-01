#pragma once

#include "esphome/components/remote_base/remote_base.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::scs_bticino {

static constexpr uint8_t SCS_START = 0xA8;
static constexpr uint8_t SCS_END = 0xA3;
static constexpr uint8_t SCS_ACK = 0xA5;
static constexpr uint8_t SCS_STANDARD_SIZE = 7;
static constexpr uint8_t SCS_EXTENDED_SIZE = 11;

struct ScsBticinoData {
  std::array<uint8_t, SCS_EXTENDED_SIZE> bytes{};
  uint8_t length{0};

  bool is_ack() const { return this->length == 1 && this->bytes[0] == SCS_ACK; }
  bool is_valid() const;
  uint8_t get_size() const { return std::min(this->length, SCS_EXTENDED_SIZE); }
  std::vector<uint8_t> get_bytes() const;
  std::vector<uint8_t> get_payload() const;
  std::string to_string() const;

  static ScsBticinoData acknowledgment();
  static bool from_payload(ScsBticinoData &data, const uint8_t *payload, uint8_t payload_length);
  static bool from_bytes(ScsBticinoData &data, const uint8_t *bytes, uint8_t length);
};

class ScsBticinoProtocol : public remote_base::RemoteProtocol<ScsBticinoData> {
 public:
  void encode(remote_base::RemoteTransmitData *dst, const ScsBticinoData &src) override;
  optional<ScsBticinoData> decode(remote_base::RemoteReceiveData src) override;
  void dump(const ScsBticinoData &data) override;

 protected:
  void encode_byte_(remote_base::RemoteTransmitData *dst, uint8_t byte, bool final_byte) const;
};

using ScsBticinoTrigger = remote_base::RemoteReceiverTrigger<ScsBticinoProtocol>;
using ScsBticinoDumper = remote_base::RemoteReceiverDumper<ScsBticinoProtocol>;

template<typename... Ts> class ScsBticinoAction : public remote_base::RemoteTransmitterActionBase<Ts...> {
 public:
  TEMPLATABLE_VALUE(bool, ack)

  void set_payload(const uint8_t *payload, size_t length) {
    this->payload_ = payload;
    this->payload_length_ = length;
  }

 protected:
  void encode(remote_base::RemoteTransmitData *dst, Ts... x) override {
    ScsBticinoData data;
    if (this->ack_.value(x...)) {
      data = ScsBticinoData::acknowledgment();
    } else if (!ScsBticinoData::from_payload(data, this->payload_, this->payload_length_)) {
      return;
    }
    ScsBticinoProtocol().encode(dst, data);
  }

  const uint8_t *payload_{nullptr};
  size_t payload_length_{0};
};

}  // namespace esphome::scs_bticino
