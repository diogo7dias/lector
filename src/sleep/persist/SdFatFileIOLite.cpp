#include "SdFatFileIOLite.h"

#include <Arduino.h>
#include <HalStorage.h>

#include <string>

namespace crosspoint {
namespace persist {
namespace {

constexpr const char* kModule = "SLP";

// Create the immediate parent directory of `path` if it does not exist. Storage
// mkdir creates intermediate dirs (pFlag defaults true).
void ensureParentDir(const std::string& path) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return;
  const std::string dir = path.substr(0, slash);
  if (!Storage.exists(dir.c_str())) Storage.mkdir(dir.c_str());
}

std::string readWhole(const std::string& path) {
  if (!Storage.exists(path.c_str())) return {};
  const String s = Storage.readFile(path.c_str());
  return std::string(s.c_str(), s.length());
}

// JsonSink that streams bytes straight to an open HalFile, so serialization
// never builds a full intermediate string. Tracks the first short write.
struct HalFileSink final : JsonSink {
  HalFile* file = nullptr;
  bool ok = true;
  size_t write(uint8_t b) override {
    const size_t n = file->write(&b, 1);
    if (n != 1) ok = false;
    return n;
  }
  size_t write(const uint8_t* buf, size_t n) override {
    const size_t w = file->write(buf, n);
    if (w != n) ok = false;
    return w;
  }
};

}  // namespace

bool SdFatFileIOLite::safeWriteStreamed(const std::string& path, const StreamProducer& produce) {
  ensureParentDir(path);
  const std::string tmp = path + ".tmp";

  HalFile f;
  if (!Storage.openFileForWrite(kModule, tmp, f)) return false;

  HalFileSink sink;
  sink.file = &f;
  const bool produced = produce(sink);
  f.close();

  if (!produced || !sink.ok) {
    Storage.remove(tmp.c_str());
    return false;
  }

  // Rotate: real -> .bak, then tmp -> real. A crash between the two leaves the
  // .bak (or the .tmp) for safeRead's fallback ladder to recover.
  if (Storage.exists(path.c_str())) {
    const std::string bak = path + ".bak";
    Storage.remove(bak.c_str());
    Storage.rename(path.c_str(), bak.c_str());
  }
  return Storage.rename(tmp.c_str(), path.c_str());
}

bool SdFatFileIOLite::safeWrite(const std::string& path, const std::string& content) {
  return safeWriteStreamed(path, [&content](JsonSink& sink) {
    if (!content.empty()) {
      sink.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
    }
    return true;
  });
}

std::string SdFatFileIOLite::safeRead(const std::string& path) {
  std::string out = readWhole(path);
  if (!out.empty()) return out;
  out = readWhole(path + ".bak");
  if (!out.empty()) return out;
  return readWhole(path + ".tmp");
}

bool SdFatFileIOLite::exists(const std::string& path) { return Storage.exists(path.c_str()); }

bool SdFatFileIOLite::mkdir(const std::string& path) { return Storage.mkdir(path.c_str()); }

bool SdFatFileIOLite::copy(const std::string& from, const std::string& to) {
  if (!Storage.exists(from.c_str())) return false;
  const String content = Storage.readFile(from.c_str());
  return Storage.writeFile(to.c_str(), content);
}

}  // namespace persist
}  // namespace crosspoint
