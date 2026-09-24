// The boot classification HalGPIO::getWakeupReason() used to make inline, as a table.
// Every row states today's shipped answer.
#include <gtest/gtest.h>

#include "WakeClassify.h"

namespace {

using wake_classify::BootFacts;
using wake_classify::classify;
using wake_classify::Reset;
using wake_classify::WakeupReason;

struct Row {
  const char* name;
  Reset reset;
  bool wakeCauseReported;
  bool usbConnected;
  bool xteinkTopology;  // coldBootImpliesPowerButton
  WakeupReason expected;
};

// "cause" in the names: TIMER / GPIO / EXT1 all mean wakeCauseReported; UNDEFINED means not.
constexpr Row kRows[] = {
    // Any deep-sleep reset is the button, whatever cause the chip reports (lector.exp.54:
    // an X4 Pro button unlock came back as cause=TIMER).
    {"DeepSleep cause=TIMER", Reset::DeepSleep, true, false, false, WakeupReason::PowerButton},
    {"DeepSleep cause=GPIO/EXT1", Reset::DeepSleep, true, false, true, WakeupReason::PowerButton},
    {"DeepSleep cause=UNDEFINED", Reset::DeepSleep, false, false, false, WakeupReason::PowerButton},
    {"DeepSleep on USB", Reset::DeepSleep, true, true, false, WakeupReason::PowerButton},

    // POWERON on battery: an X4 unlock (the lock cut the battery latch) is the button;
    // the X4 Pro / other boards cannot trust a cold boot to mean that.
    {"PowerOn battery X4 topology", Reset::PowerOn, false, false, true, WakeupReason::PowerButton},
    {"PowerOn battery other topology", Reset::PowerOn, false, false, false, WakeupReason::Other},

    // POWERON with USB: plugging into an off device charge-sleeps, on every topology.
    {"PowerOn USB X4 topology", Reset::PowerOn, false, true, true, WakeupReason::AfterUSBPower},
    {"PowerOn USB other topology", Reset::PowerOn, false, true, false, WakeupReason::AfterUSBPower},

    // A fresh flash resets as UNKNOWN with the cable in.
    {"Unknown USB (flash)", Reset::Unknown, false, true, false, WakeupReason::AfterFlash},
    {"Unknown USB X4 topology", Reset::Unknown, false, true, true, WakeupReason::AfterFlash},
    {"Unknown battery", Reset::Unknown, false, false, true, WakeupReason::Other},

    // Outside deep sleep, a reported wake cause disqualifies every other rule.
    {"PowerOn battery X4 with cause", Reset::PowerOn, true, false, true, WakeupReason::Other},
    {"PowerOn USB with cause", Reset::PowerOn, true, true, false, WakeupReason::Other},
    {"Unknown USB with cause", Reset::Unknown, true, true, false, WakeupReason::Other},

    // Software restart, panic, watchdog, brownout: all Reset::Other, all a plain boot.
    {"Software/panic battery X4", Reset::Other, false, false, true, WakeupReason::Other},
    {"Software/panic USB", Reset::Other, false, true, false, WakeupReason::Other},
};

}  // namespace

TEST(WakeClassify, Table) {
  for (const Row& row : kRows) {
    BootFacts f;
    f.reset = row.reset;
    f.wakeCauseReported = row.wakeCauseReported;
    f.usbConnected = row.usbConnected;
    f.coldBootImpliesPowerButton = row.xteinkTopology;
    EXPECT_EQ(classify(f), row.expected) << row.name;
  }
}

// Compile-time too: the classifier is constexpr so the adapter costs nothing extra.
static_assert(classify(BootFacts{Reset::DeepSleep, true, false, false}) == WakeupReason::PowerButton);
