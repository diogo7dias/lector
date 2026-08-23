#pragma once

#include <Arduino.h>
#include <Rtc.h>
#include <time.h>

class HalClock;
extern HalClock halClock;  // Singleton

class HalClock {
  bool _available = false;
  mutable Rtc _sdkRtc;
  mutable uint8_t _cachedHour = 0;
  mutable uint8_t _cachedMinute = 0;
  mutable bool _hasCachedTime = false;
  mutable unsigned long _lastPollMs = 0;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds
  // 2020-01-01T00:00:00Z. An unsynced system clock starts at the epoch, and a reading
  // day recorded in 1970 is worse than no reading day at all.
  static constexpr time_t SYSTEM_CLOCK_VALID_FROM = 1577836800;

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // True if an RTC is present on this device
  bool isAvailable() const { return _available; }

  // Get current hour (0-23) and minute (0-59).
  // Returns false if RTC is not available.
  bool getTime(uint8_t& hour, uint8_t& minute) const;

  // Full calendar date plus time, in UTC as the RTC holds it. Reading stats need
  // a date, not just a clock, to bucket by weekday and count reading streaks.
  // Falls back to the system clock on boards with no RTC, which is only useful
  // after an NTP sync; an unset system clock is rejected rather than reported as
  // the year 1970.
  bool getDateTime(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute) const;

  // True when getDateTime() would succeed: an RTC that answers, or a system clock some
  // NTP sync has set since the last power-off.
  bool hasDate() const;

  // Format time into a caller-provided buffer.
  // 24h mode produces "HH:MM" (needs >=6 bytes); 12h mode produces "H:MM AM"/"HH:MM PM" (needs >=9 bytes).
  // utcOffsetQuarterHoursBiased: biased quarter-hour offset (48 = UTC+0, 0 = UTC-12, 104 = UTC+14).
  // use12Hour: when true, format as 12-hour clock with AM/PM suffix.
  // Returns false if RTC is not available.
  bool formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48, bool use12Hour = false) const;

  // Sync from an NTP server. Requires WiFi to be connected. Sets the system clock, and
  // the RTC too on a board that has one.
  // Blocks for up to ~5s while waiting for SNTP response.
  // Returns true if the clock was successfully updated.
  //
  // Debouncing (skip if already synced once) is enforced by the caller, not here,
  // so the HAL stays free of any app-layer settings dependency.
  bool syncFromNTP();
};
