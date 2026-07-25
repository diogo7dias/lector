#pragma once

#include "SleepImageMove.h"

namespace crosspoint {
namespace sleep {

// Production ISleepImageFs backed by HalStorage. Excluded from the host test
// build — HalStorage pulls in ESP32-only headers. The bulk-move algorithm it
// serves lives in SleepImageMove.h and is tested there against a fake.
class SdSleepImageFs final : public ISleepImageFs {
 public:
  void walk(const char* dir, const NameSink& sink) override;
  bool mkdir(const char* path) override;
  bool rename(const char* from, const char* to) override;
};

}  // namespace sleep
}  // namespace crosspoint
