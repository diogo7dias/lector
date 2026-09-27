#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <string>

// SD side of the "<book>_QUOTES.txt" sidecar (the string side is QuoteText.h).
namespace quote_sidecar {

// Last step of both sidecar writers: `path + ".tmp"` holds the complete new file.
// Rotate the live file to ".bak", promote the tmp, then drop the backup. A failed
// promote puts the backup back; any failure removes the tmp. The ".bak" left only by
// a crash between the renames is what the viewer's load falls back to.
inline bool promoteTmp(const std::string& path, const char* tag) {
  const std::string tmpPath = path + ".tmp";
  const std::string bakPath = path + ".bak";
  if (Storage.exists(path.c_str())) {
    Storage.remove(bakPath.c_str());  // clear any stale backup
    if (!Storage.rename(path.c_str(), bakPath.c_str())) {
      LOG_ERR(tag, "Quotes backup rename failed");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR(tag, "Quotes promote rename failed");
    if (Storage.exists(bakPath.c_str())) Storage.rename(bakPath.c_str(), path.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  Storage.remove(bakPath.c_str());
  return true;
}

}  // namespace quote_sidecar
