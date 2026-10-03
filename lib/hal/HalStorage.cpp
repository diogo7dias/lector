#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <Memory.h>
#include <SDCardManager.h>
#if FREEINK_CAP_USB_MSC
#include <UsbMassStorage.h>
#endif

#include <cassert>

#if FREEINK_SD_SDMMC
#include <driver/gpio.h>
#endif

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

namespace {
// Set once beginUsbDrive() hands the card to the host, read under storageMutex, never
// cleared: the way out of USB Drive is a reboot.
bool cardLentToUsbHost = false;
#if FREEINK_CAP_USB_MSC
freeink::UsbMassStorage usbMassStorage;
#endif
}  // namespace

HalStorage::HalStorage() {
  // Recursive so the same task can re-enter StorageLock without self-deadlock.
  // openFileForRead/Write take the lock and then assign to a HalFile&
  // out-param; if that out-param already held an Impl, its destructor takes
  // the lock again to close the prior FsFile under serialization (see
  // HalFile::Impl::~Impl below). Priority inheritance still applies to
  // recursive mutexes.
  storageMutex = xSemaphoreCreateRecursiveMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

// Port of upstream SDCardManager::shutdown() (freeink-sdk fad70f28a), built from the
// SDK's existing public hooks so the pinned submodule stays untouched.
void HalStorage::prepareForDeepSleep() {
#if FREEINK_SD_SDMMC
  StorageLock lock;
  if (cardLentToUsbHost) return;
  // Unmounts the FsVolume (flushing SdFat's cached FAT/dir sectors) and returns the
  // block device; nullptr when nothing is mounted.
  FsBlockDeviceInterface* dev = SDCard.detachFilesystemForRawAccess();
  if (!dev) return;
  dev->end();  // frees the card and runs sdmmc_host_deinit()
  // sdmmc_host_deinit() leaves CLK/CMD/D0-D3 idling high, back-feeding the card's
  // VDD net through the bus pull-ups for the whole sleep. Float them.
  const BoardConfig::SdmmcPins& p = BoardConfig::ACTIVE.sdmmc;
  for (const int8_t pin : {p.clk, p.cmd, p.d0, p.d1, p.d2, p.d3}) {
    if (pin < 0) continue;
    const auto g = static_cast<gpio_num_t>(pin);
    gpio_set_direction(g, GPIO_MODE_INPUT);
    gpio_pullup_dis(g);
    gpio_pulldown_dis(g);
  }
#endif
  // SPI boards: sleep cuts or gates the SD rail; there is no host to stop.
}

#if FREEINK_CAP_USB_MSC && !FREEINK_SD_SDMMC
#error "USB Drive needs the SDMMC storage backend"
#endif

bool HalStorage::beginUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  // Lent before the volume is detached: a file opened earlier still points at the
  // detached volume's sectors and must fail from here on, not write through it.
  cardLentToUsbHost = true;
  auto* const blockDevice = SDCard.detachFilesystemForRawAccess();
  if (!blockDevice) {
    LOG_ERR("USB", "USB Drive needs a mounted SD card");
    return false;
  }
  if (!usbMassStorage.begin(blockDevice)) {
    LOG_ERR("USB", "USB Drive MSC initialization failed");
    return false;
  }
  return true;
#else
  return false;
#endif
}

bool HalStorage::disconnectUsbDriveHost() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  return usbMassStorage.disconnectHost();
#else
  return false;
#endif
}

void HalStorage::endUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  usbMassStorage.end();
#endif
}

UsbDriveState HalStorage::usbDriveState() const {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  switch (usbMassStorage.state()) {
    case freeink::UsbMassStorageState::WaitingForHost:
      return UsbDriveState::WaitingForHost;
    case freeink::UsbMassStorageState::Connected:
    case freeink::UsbMassStorageState::Accessed:
      return UsbDriveState::Connected;
    case freeink::UsbMassStorageState::Ejected:
      return UsbDriveState::Ejected;
    case freeink::UsbMassStorageState::Disconnected:
      return UsbDriveState::Disconnected;
    case freeink::UsbMassStorageState::IoError:
      return UsbDriveState::IoError;
    case freeink::UsbMassStorageState::Idle:
      break;
  }
#endif
  return UsbDriveState::Unsupported;
}

// For the rest of the methods, we acquire the mutex to ensure thread safety

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  if (cardLentToUsbHost) return {};           \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

uint64_t HalStorage::sdTotalBytes() { HAL_STORAGE_WRAPPED_CALL(sdTotalBytes); }

uint64_t HalStorage::sdUsedBytes() { HAL_STORAGE_WRAPPED_CALL(sdUsedBytes); }

class HalFile::Impl {
 public:
  // SdFat is not thread-safe; FsFile::close() touches SD/SPI and must run
  // under StorageLock or it races SdSpiCard::m_spiActive across tasks and
  // trips FreeRTOS's xTaskPriorityDisinherit assert. The FsFile member
  // destructor (DESTRUCTOR_CLOSES_FILE=1) will close() again after the lock
  // releases, but close() on an already-closed FsFile is a no-op. See SdFat
  // issue #518 and the HAL note in CLAUDE.md.
  ~Impl() {
    HalStorage::StorageLock lock;
    // Forget, not close: closing syncs dirty sectors, and the card is the host's now.
    if (cardLentToUsbHost) {
      file = FsFile();
      return;
    }
    file.close();
  }
  FsFile file;
};

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  // Allocate before opening: O_TRUNC may already change the card even if the
  // handle allocation then fails. Throwing new would abort without cleanup.
  auto impl = makeUniqueNoThrow<HalFile::Impl>();
  if (!impl) {
    LOG_ERR("SD", "OOM: file handle");
    return {};
  }
  if (cardLentToUsbHost) return {};
  impl->file = SDCard.open(path, oflag);
  return HalFile(std::move(impl));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  auto impl = makeUniqueNoThrow<HalFile::Impl>();
  if (!impl) {
    LOG_ERR(moduleName, "OOM: file handle");
    file = HalFile();
    return false;
  }
  const bool ok = !cardLentToUsbHost && SDCard.openFileForRead(moduleName, path, impl->file);
  file = HalFile(std::move(impl));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  auto impl = makeUniqueNoThrow<HalFile::Impl>();
  if (!impl) {
    LOG_ERR(moduleName, "OOM: file handle");
    file = HalFile();
    return false;
  }
  const bool ok = !cardLentToUsbHost && SDCard.openFileForWrite(moduleName, path, impl->file);
  file = HalFile(std::move(impl));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile class in HalStorage.h

#define HAL_FILE_WRAPPED_CALL(method, ...)                                  \
  HalStorage::StorageLock lock;                                             \
  assert(impl != nullptr);                                                  \
  if (cardLentToUsbHost) return decltype(impl->file.method(__VA_ARGS__))(); \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
bool HalFile::getModifyDateTime(uint16_t* pdate, uint16_t* ptime) {
  HAL_FILE_WRAPPED_CALL(getModifyDateTime, pdate, ptime);
}
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe, no need to wrap
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  if (cardLentToUsbHost) return -1;
  return impl->file.read();
}
size_t HalFile::write(const uint8_t* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() {
  if (!impl) return false;  // cleanup after a failed handle allocation
  HAL_FILE_WRAPPED_CALL(close, );
}
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto next = makeUniqueNoThrow<Impl>();
  if (!next) {
    LOG_ERR("SD", "OOM: directory entry handle");
    return {};
  }
  if (cardLentToUsbHost) return {};
  next->file = impl->file.openNextFile();
  return HalFile(std::move(next));
}
bool HalFile::isOpen() const {
  return impl != nullptr && !cardLentToUsbHost && impl->file.isOpen();
}  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
