#pragma once

#include <Print.h>
#include <common/FsApiConstants.h>  // for oflag_t
#include <freertos/semphr.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class HalFile;

enum class UsbDriveState : uint8_t {
  Unsupported,
  WaitingForHost,
  Connected,
  Ejected,
  Disconnected,
  IoError,
};

class HalStorage {
 public:
  HalStorage();
  bool begin();
  bool ready() const;
  // Stop the SD card for deep sleep: flush the volume, unmount, stop the SDMMC host,
  // and float the bus pads (flush only on SPI boards). A failed flush is logged and
  // sleep goes on. Call only after all file users have stopped;
  // open HalFiles become invalid. A deep-sleep wake resets the MCU and mounts
  // storage again through begin().
  void prepareForDeepSleep();
  // USB Drive lends the raw SD card to a USB host. From beginUsbDrive() on, every
  // HalStorage and HalFile call that would reach the card fails instead, including on
  // handles opened before the handoff (isOpen() turns false), so no firmware path can
  // write behind the host's back. There is no way back: the caller reboots after
  // endUsbDrive().
  bool beginUsbDrive();
  bool disconnectUsbDriveHost();
  void endUsbDrive();
  UsbDriveState usbDriveState() const;
  // The bus is suspended: the host slept, or the cable is out on a battery-powered
  // board, where TinyUSB still reports mounted (upstream #3538).
  bool usbDriveHostSuspended() const;
  // Read the entire file at `path` into a String. Returns empty string on failure.
  String readFile(const char* path);
  // Read up to `bufferSize-1` bytes into `buffer`, null-terminating it. Returns bytes read.
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  // Write a string to `path` on the SD card. Overwrites existing file.
  // Returns true on success.
  bool writeFile(const char* path, const String& content);
  // Ensure a directory exists, creating it if necessary. Returns true on success.
  bool ensureDirectoryExists(const char* path);

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);

  // Counts every removal, rename or write-open of a path named like a book (EPUB, XTC, text).
  // Every channel that changes the card's books goes through this class, so a screen that
  // keeps a picture of the card (the Library index) compares it with the value it saw.
  // Wraps; only equality means anything.
  uint32_t bookGeneration() const { return bookGen; }
  // Called by the methods above, and by HalFile::rename; call it with the storage mutex held.
  void noteBookPath(const char* path);
  bool rmdir(const char* path);

  // Card capacity and used space in bytes, 0 when no card is mounted. The used
  // figure walks the FAT, so SDCardManager caches it with a short TTL; callers
  // may poll it per request without paying for a scan each time.
  uint64_t sdTotalBytes();
  uint64_t sdUsedBytes();

  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForRead(const char* moduleName, const String& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file);
  bool removeDir(const char* path);

  static HalStorage& getInstance() { return instance; }

  // Serialises every SD operation. Public so a caller can hold one across a
  // multi-step read: the per-call locking inside HalFile keeps one seek or one
  // read atomic, but a seek-then-read pair on a shared handle can still be split
  // by another task seeking the same handle, which returns the wrong record.
  // Recursive, so nesting it inside HalStorage's own calls is safe.
  class StorageLock {
   public:
    StorageLock() {
      xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY);
      holdNormalSpeed();
    }
    ~StorageLock() { xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex); }
    StorageLock(const StorageLock&) = delete;
    StorageLock& operator=(const StorageLock&) = delete;

   private:
    static void holdNormalSpeed();  // HalPowerManager::holdNormalSpeedForStorage()
  };

  // For HalPowerManager only: it lowers the CPU clock with the card idle and locked out.
  bool tryLock() { return xSemaphoreTakeRecursive(storageMutex, 0) != 0; }
  void unlock() { xSemaphoreGiveRecursive(storageMutex); }

 private:
  static HalStorage instance;

  bool initialized = false;
  volatile uint32_t bookGen = 0;
  SemaphoreHandle_t storageMutex = nullptr;
};

#define Storage HalStorage::getInstance()

class HalFile : public Print {
  friend class HalStorage;
  class Impl;
  std::unique_ptr<Impl> impl;
  explicit HalFile(std::unique_ptr<Impl> impl);

 public:
  HalFile();
  ~HalFile();
  HalFile(HalFile&&);
  HalFile& operator=(HalFile&&);
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush();
  size_t getName(char* name, size_t len);
  bool getModifyDateTime(uint16_t* pdate, uint16_t* ptime);
  size_t size();
  size_t fileSize();
  uint64_t fileSize64();
  bool seek(size_t pos);
  bool seek64(uint64_t pos);
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  int read();  // read a single byte
  size_t write(const uint8_t* buf, size_t count) override;
  size_t write(const void* buf, size_t count);
  size_t write(uint8_t b) override;
  bool rename(const char* newPath);
  bool isDirectory() const;
  void rewindDirectory();
  bool close();
  HalFile openNextFile();
  bool isOpen() const;
  operator bool() const;
};

// Downstream code must use Storage instead of SdMan
#ifdef SdMan
#undef SdMan
#endif
