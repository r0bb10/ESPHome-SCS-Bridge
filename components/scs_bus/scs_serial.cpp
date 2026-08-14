#include "scs_serial.h"

namespace esphome::scs_bus {

size_t scs_encode_serial_byte(uint8_t byte, ScsSerialRun *runs, size_t capacity) {
  constexpr uint16_t MAX_DURATION_TICKS = 0x7FFF;
  size_t run_count = 0;
  const auto append_run = [runs, capacity, &run_count](bool level, uint16_t duration_ticks) {
    while (duration_ticks != 0) {
      if (run_count > 0 && runs[run_count - 1].level == level &&
          runs[run_count - 1].duration_ticks < MAX_DURATION_TICKS) {
        const uint16_t available = MAX_DURATION_TICKS - runs[run_count - 1].duration_ticks;
        const uint16_t added = duration_ticks < available ? duration_ticks : available;
        runs[run_count - 1].duration_ticks += added;
        duration_ticks -= added;
      } else {
        if (run_count == capacity)
          return false;
        const uint16_t added = duration_ticks < MAX_DURATION_TICKS ? duration_ticks : MAX_DURATION_TICKS;
        runs[run_count++] = {level, added};
        duration_ticks -= added;
      }
    }
    return true;
  };

  if (!append_run(false, ScsSerialReceiver::ACTIVE_TICKS) ||
      !append_run(true, ScsSerialReceiver::CELL_TICKS - ScsSerialReceiver::ACTIVE_TICKS))
    return 0;
  for (uint8_t bit = 0; bit < 8; bit++) {
    if ((byte & (1U << bit)) == 0) {
      if (!append_run(false, ScsSerialReceiver::ACTIVE_TICKS) ||
          !append_run(true, ScsSerialReceiver::CELL_TICKS - ScsSerialReceiver::ACTIVE_TICKS))
        return 0;
    } else if (!append_run(true, ScsSerialReceiver::CELL_TICKS)) {
      return 0;
    }
  }
  return append_run(true, ScsSerialReceiver::CELL_TICKS) ? run_count : 0;
}

}  // namespace esphome::scs_bus
