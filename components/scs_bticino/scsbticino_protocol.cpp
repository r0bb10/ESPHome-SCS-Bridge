#include "scsbticino_protocol.h"

#include "esphome/core/log.h"

namespace esphome::scs_bticino {

namespace {

static const char *const TAG = "remote.scs_bticino";

}  // namespace

void ScsBticinoProtocol::encode(remote_base::RemoteTransmitData *dst, const ScsBticinoData &src) {
  std::vector<ScsBticinoRun> runs;
  if (!ScsBticinoCodec::encode(src, &runs)) {
    ESP_LOGW(TAG, "Refusing invalid SCS frame");
    return;
  }
  dst->set_carrier_frequency(0);
  dst->reserve(runs.size());
  for (const auto &run : runs) {
    if (run.released)
      dst->space(run.duration_us);
    else
      dst->mark(run.duration_us);
  }
  ESP_LOGD(TAG, "Transmitting: %s", src.to_string().c_str());
}

optional<ScsBticinoData> ScsBticinoProtocol::decode(remote_base::RemoteReceiveData src) {
  std::vector<ScsBticinoRun> runs;
  runs.reserve(src.get_raw_data().size());
  for (const int32_t timing : src.get_raw_data()) {
    if (timing == 0)
      continue;
    const bool released = timing > 0;
    const uint32_t duration = timing < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(timing))
                                         : static_cast<uint32_t>(timing);
    if (!runs.empty() && runs.back().released == released) {
      runs.back().duration_us += duration;
    } else {
      runs.push_back({released, duration});
    }
  }

  ScsBticinoData data;
  if (!ScsBticinoCodec::decode(runs, &data))
    return {};
  return data;
}

void ScsBticinoProtocol::dump(const ScsBticinoData &data) {
  ESP_LOGI(TAG, "Received: %s", data.to_string().c_str());
}

}  // namespace esphome::scs_bticino
