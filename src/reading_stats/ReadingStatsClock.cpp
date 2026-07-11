#include "ReadingStatsClock.h"

#include <HalClock.h>

#include "CrossPointSettings.h"

namespace reading_stats {

LocalDateTime currentLocalDateTime() {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  LocalDateTime local;
  if (!halClock.getDateTime(year, month, day, hour, minute)) return local;
  const int offsetQuarterHours = static_cast<int>(SETTINGS.clockUtcOffsetQ) - 48;
  makeLocalDateTime({year, month, day}, hour, minute, 0, offsetQuarterHours, local);
  return local;
}

}  // namespace reading_stats
