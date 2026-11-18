#pragma once

// Host stand-in for lib/Serialization/PersistableStore.h: the singleton and the password
// helper, with no SD card. saveToFile only reports success; the tests drive toJson and
// fromJson directly.
#include <ArduinoJson.h>

#include <string>

#include "ObfuscationUtils.h"

class PersistableStoreBase {
 protected:
  void requestResave() { resaveRequested = true; }
  bool resaveRequested = false;

  static std::string extractPassword(JsonVariantConst doc, bool&) {
    bool ok = false;
    bool tooLong = false;
    return obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", 1024, &ok, &tooLong);
  }
};

template <typename T>
class PersistableStore : public PersistableStoreBase {
 protected:
  PersistableStore() = default;

 public:
  static T& getInstance() {
    static T instance;
    return instance;
  }
  bool saveToFile() const { return true; }
};
