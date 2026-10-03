#pragma once
// Records every call that reaches the card, so the test can prove none do while
// USB Drive holds it.
#include <BoardConfig.h>
#include <WString.h>
#include <common/FsApiConstants.h>

#include <cstddef>
#include <cstdint>
#include <vector>

inline int cardCalls = 0;

class FsBlockDeviceInterface {
 public:
  virtual ~FsBlockDeviceInterface() = default;
  virtual void end() {}
};

class FsFile {
 public:
  FsFile() = default;
  FsFile(const FsFile&) = default;
  FsFile& operator=(const FsFile&) = default;
  ~FsFile() {
    if (open_) close();
  }
  bool close() {
    if (open_) ++cardCalls;
    open_ = false;
    return true;
  }
  bool isOpen() const { return open_; }
  void flush() { ++cardCalls; }
  size_t getName(char*, size_t) { return ++cardCalls; }
  bool getModifyDateTime(uint16_t*, uint16_t*) { return ++cardCalls; }
  size_t size() { return 0; }
  size_t fileSize() { return 0; }
  bool seekSet(uint64_t) { return ++cardCalls; }
  bool seekCur(int64_t) { return ++cardCalls; }
  int available() const { return ++cardCalls; }
  uint64_t position() const { return ++cardCalls; }
  int read(void*, size_t n) { return ++cardCalls, static_cast<int>(n); }
  int read() { return ++cardCalls; }
  size_t write(const void*, size_t n) { return ++cardCalls, n; }
  size_t write(uint8_t) { return ++cardCalls; }
  bool rename(const char*) { return ++cardCalls; }
  bool isDirectory() const { return false; }
  void rewindDirectory() { ++cardCalls; }
  FsFile openNextFile() { return ++cardCalls, opened(); }

  static FsFile opened() {
    FsFile f;
    f.open_ = true;
    return f;
  }

 private:
  bool open_ = false;
};

class SDCardManager {
 public:
  bool begin() { return true; }
  bool ready() const { return true; }
  FsBlockDeviceInterface* detachFilesystemForRawAccess() { return &device; }
  std::vector<String> listFiles(const char*, int) { return ++cardCalls, std::vector<String>{"a"}; }
  String readFile(const char*) { return ++cardCalls, String("x"); }
  template <class P>
  bool readFileToStream(const char*, P&, size_t) {
    return ++cardCalls;
  }
  size_t readFileToBuffer(const char*, char*, size_t, size_t) { return ++cardCalls; }
  bool writeFile(const char*, const String&) { return ++cardCalls; }
  bool ensureDirectoryExists(const char*) { return ++cardCalls; }
  uint64_t sdTotalBytes() { return ++cardCalls; }
  uint64_t sdUsedBytes() { return ++cardCalls; }
  FsFile open(const char*, oflag_t) { return ++cardCalls, FsFile::opened(); }
  bool mkdir(const char*, bool) { return ++cardCalls; }
  bool exists(const char*) { return ++cardCalls; }
  bool remove(const char*) { return ++cardCalls; }
  bool rename(const char*, const char*) { return ++cardCalls; }
  bool rmdir(const char*) { return ++cardCalls; }
  bool removeDir(const char*) { return ++cardCalls; }
  bool openFileForRead(const char*, const char*, FsFile& f) { return ++cardCalls, f = FsFile::opened(), true; }
  bool openFileForWrite(const char*, const char*, FsFile& f) { return ++cardCalls, f = FsFile::opened(), true; }

  static SDCardManager& getInstance() {
    static SDCardManager instance;
    return instance;
  }

 private:
  FsBlockDeviceInterface device;
};
