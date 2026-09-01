#pragma once

#include "esphome/components/remote_base/remote_base.h"

#include "scsbticino_codec.h"

namespace esphome::scs_bticino {

class ScsBticinoProtocol : public remote_base::RemoteProtocol<ScsBticinoData> {
 public:
  void encode(remote_base::RemoteTransmitData *dst, const ScsBticinoData &src) override;
  optional<ScsBticinoData> decode(remote_base::RemoteReceiveData src) override;
  void dump(const ScsBticinoData &data) override;

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
