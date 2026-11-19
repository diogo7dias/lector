#pragma once
// Host-test stub for HalStorage over a real temp directory: `fake::root` stands in for the
// card's root, so the builder walks real directories and writes real files.
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fake {
inline std::string root;
// Directory entries listed twice, as a damaged FAT does: dir path -> names.
inline std::map<std::string, std::vector<std::string>> duplicates;
inline int failRenameCount = -1;  // fail the Nth rename from now (0 = next), -1 = never
inline int failWriteBytes = -1;   // fail every write after this many bytes, -1 = never
inline size_t written = 0;

inline std::string real(const std::string& path) { return root + path; }
}  // namespace fake

class HalFile {
 public:
  HalFile() = default;
  HalFile(HalFile&& o) noexcept { *this = std::move(o); }
  HalFile& operator=(HalFile&& o) noexcept {
    if (this == &o) return *this;
    close();
    file = o.file;
    o.file = nullptr;
    path = std::move(o.path);
    dir = o.dir;
    entries = std::move(o.entries);
    cursor = o.cursor;
    opened = o.opened;
    o.opened = false;
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  ~HalFile() { close(); }

  bool open(const std::string& virtualPath, const char* mode) {
    close();
    path = virtualPath;
    const std::string real = fake::real(virtualPath);
    if (std::filesystem::is_directory(real)) {
      dir = true;
      for (const auto& e : std::filesystem::directory_iterator(real)) entries.push_back(e.path().filename().string());
      std::sort(entries.begin(), entries.end());
      const auto dup = fake::duplicates.find(virtualPath);
      if (dup != fake::duplicates.end()) entries.insert(entries.end(), dup->second.begin(), dup->second.end());
      opened = true;
      return true;
    }
    file = std::fopen(real.c_str(), mode);
    opened = file != nullptr;
    return opened;
  }
  explicit operator bool() const { return opened; }
  bool isOpen() const { return opened; }
  bool isDirectory() const { return dir; }
  bool close() {
    if (file) std::fclose(file);
    file = nullptr;
    opened = false;
    return true;
  }
  void flush() {
    if (file) std::fflush(file);
  }
  HalFile openNextFile() {
    HalFile next;
    if (!dir || cursor >= entries.size()) return next;
    const std::string name = entries[cursor++];
    next.open((path == "/" ? "" : path) + "/" + name, "rb");
    return next;
  }
  size_t getName(char* out, const size_t size) {
    const std::string name = path.substr(path.find_last_of('/') + 1);
    std::strncpy(out, name.c_str(), size);
    out[size - 1] = '\0';
    return name.size();
  }
  bool getModifyDateTime(uint16_t* date, uint16_t* time) {
    struct stat st{};
    if (stat(fake::real(path).c_str(), &st) != 0) return false;
    *date = static_cast<uint16_t>(st.st_mtime >> 16);
    *time = static_cast<uint16_t>(st.st_mtime & 0xFFFF);
    return true;
  }
  size_t fileSize() { return static_cast<size_t>(fileSize64()); }
  uint64_t fileSize64() {
    if (dir) return 0;
    return std::filesystem::file_size(fake::real(path));
  }
  size_t position() const { return dir ? cursor : (file ? static_cast<size_t>(std::ftell(file)) : 0); }
  bool seekSet(const size_t offset) {
    if (dir) {
      cursor = offset;
      return offset <= entries.size();
    }
    return file && std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
  }
  int read(void* buf, const size_t count) { return file ? static_cast<int>(std::fread(buf, 1, count, file)) : -1; }
  size_t write(const void* buf, const size_t count) {
    if (!file) return 0;
    if (fake::failWriteBytes >= 0 && fake::written + count > static_cast<size_t>(fake::failWriteBytes)) return 0;
    fake::written += count;
    return std::fwrite(buf, 1, count, file);
  }

 private:
  std::FILE* file = nullptr;
  std::string path;
  bool dir = false;
  std::vector<std::string> entries;
  size_t cursor = 0;
  bool opened = false;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  HalFile open(const char* path) {
    HalFile f;
    f.open(path, "rb");
    return f;
  }
  bool ensureDirectoryExists(const char* path) {
    std::filesystem::create_directories(fake::real(path));
    return true;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
  bool openFileForRead(const char* m, const std::string& path, HalFile& file) {
    return openFileForRead(m, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    fake::written = 0;
    return file.open(path, "wb");
  }
  bool openFileForWrite(const char* m, const std::string& path, HalFile& file) {
    return openFileForWrite(m, path.c_str(), file);
  }
  bool exists(const char* path) { return std::filesystem::exists(fake::real(path)); }
  bool remove(const char* path) { return std::remove(fake::real(path).c_str()) == 0; }
  bool rename(const char* from, const char* to) {
    if (fake::failRenameCount == 0) {
      fake::failRenameCount = -1;
      return false;
    }
    if (fake::failRenameCount > 0) fake::failRenameCount--;
    // FAT rename never overwrites.
    if (std::filesystem::exists(fake::real(to))) return false;
    std::error_code ec;
    std::filesystem::rename(fake::real(from), fake::real(to), ec);
    return !ec;
  }
};

#define Storage HalStorage::getInstance()
