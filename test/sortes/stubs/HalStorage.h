#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef O_RDWR
#define O_RDWR 2
#endif

// The production scanner/progress writer run against a real temporary host directory.
class HalFile {
  struct CloseFile {
    void operator()(FILE* f) const { std::fclose(f); }
  };
  std::unique_ptr<FILE, CloseFile> file;
  std::filesystem::path path;
  std::vector<std::filesystem::path> entries;
  size_t cursor = 0;
  bool directory = false;

 public:
  HalFile() = default;
  explicit HalFile(const std::filesystem::path& p, const char* mode = "rb") : path(p) {
    directory = std::filesystem::is_directory(p);
    if (directory) {
      for (const auto& entry : std::filesystem::directory_iterator(p)) entries.push_back(entry.path());
      std::sort(entries.begin(), entries.end());
    } else
      file.reset(std::fopen(p.c_str(), mode));
  }
  explicit operator bool() const { return directory || file != nullptr; }
  bool isDirectory() const { return directory; }
  HalFile openNextFile() { return cursor < entries.size() ? HalFile(entries[cursor++]) : HalFile{}; }
  size_t getName(char* out, size_t size) {
    const auto name = path.filename().string();
    std::snprintf(out, size, "%s", name.c_str());
    return std::min(name.size(), size - 1);
  }
  size_t position() const { return directory ? cursor : (file ? static_cast<size_t>(std::ftell(file.get())) : 0); }
  uint64_t fileSize64() const {
    std::error_code ec;
    return !directory && std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0;
  }
  bool seekSet(size_t pos) {
    if (directory) {
      cursor = pos;
      return pos <= entries.size();
    }
    return file && std::fseek(file.get(), static_cast<long>(pos), SEEK_SET) == 0;
  }
  int read(void* out, size_t count) { return file ? std::fread(out, 1, count, file.get()) : -1; }
  size_t write(const void* data, size_t count) { return file ? std::fwrite(data, 1, count, file.get()) : 0; }
  bool close() { return !file || std::fclose(file.release()) == 0; }
  void flush() {
    if (file) std::fflush(file.get());
  }
};
class HalStorage {
 public:
  std::filesystem::path root;
  int mutations = 0;
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  std::filesystem::path resolve(const std::string& p) const { return root / std::filesystem::path(p).relative_path(); }
  HalFile open(const char* path, int flags = 0) { return HalFile(resolve(path), flags == O_RDWR ? "r+b" : "rb"); }
  bool remove(const char* path) {
    ++mutations;
    return std::remove(resolve(path).c_str()) == 0;
  }
  bool rename(const char* from, const char* to) {
    ++mutations;
    return std::rename(resolve(from).c_str(), resolve(to).c_str()) == 0;
  }
  bool exists(const char* path) const { return std::filesystem::exists(resolve(path)); }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    file = HalFile(resolve(path), "rb");
    return bool(file);
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    ++mutations;
    file = HalFile(resolve(path), "wb");
    return bool(file);
  }
};
#define Storage HalStorage::getInstance()
