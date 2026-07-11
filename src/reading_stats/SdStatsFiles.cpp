#include "SdStatsFiles.h"

#include <HalStorage.h>
#include <Logging.h>

namespace reading_stats {

bool SdStatsFiles::read(const std::string& path, uint8_t* destination, const size_t capacity, size_t& size) {
  HalFile file;
  if (!Storage.openFileForRead("RSTAT", path, file)) return false;
  size = file.fileSize();
  if (size > capacity) {
    file.close();
    return false;
  }
  const int bytesRead = file.read(destination, size);
  file.close();
  return bytesRead >= 0 && static_cast<size_t>(bytesRead) == size;
}

bool SdStatsFiles::write(const std::string& path, const uint8_t* source, const size_t size) {
  const size_t separator = path.find_last_of('/');
  if (separator != std::string::npos && separator > 0) Storage.mkdir(path.substr(0, separator).c_str());

  HalFile file;
  if (!Storage.openFileForWrite("RSTAT", path, file)) return false;
  const size_t bytesWritten = file.write(source, size);
  file.flush();
  const bool closed = file.close();
  if (bytesWritten != size || !closed) {
    LOG_ERR("RSTAT", "Short or failed stats write: %s (%u/%u)", path.c_str(), static_cast<unsigned>(bytesWritten),
            static_cast<unsigned>(size));
    return false;
  }
  return true;
}

bool SdStatsFiles::exists(const std::string& path) const { return Storage.exists(path.c_str()); }

bool SdStatsFiles::remove(const std::string& path) { return Storage.remove(path.c_str()); }

bool SdStatsFiles::rename(const std::string& from, const std::string& to) {
  return Storage.rename(from.c_str(), to.c_str());
}

}  // namespace reading_stats
