#pragma once

#include "IFileIO.h"

namespace crosspoint {
namespace persist {

// Thin production IFileIO over lector's global Storage (HalStorage). Streams
// writes straight to a .tmp file and rotates .tmp -> .bak -> real so a crash
// mid-write never corrupts the live file; reads fall back real -> .bak -> .tmp.
// "Lite" vs DX34's SdFatFileIO: no PersistManager / async-writer coupling — it
// is a direct synchronous adapter, all the sleep order file needs.
class SdFatFileIOLite final : public IFileIO {
 public:
  bool safeWrite(const std::string& path, const std::string& content) override;
  bool safeWriteStreamed(const std::string& path, const StreamProducer& produce) override;
  std::string safeRead(const std::string& path) override;
  size_t readableSize(const std::string& path) override;
  bool readStreamed(const std::string& path, std::string& out) override;
  bool exists(const std::string& path) override;
  bool mkdir(const std::string& path) override;
  bool copy(const std::string& from, const std::string& to) override;
};

}  // namespace persist
}  // namespace crosspoint
