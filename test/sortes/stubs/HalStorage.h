#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
  size_t position() const { return cursor; }
  bool seekSet(size_t pos) {
    cursor = pos;
    return pos <= entries.size();
  }
  int read(void* out, size_t count) { return file ? std::fread(out, 1, count, file.get()) : -1; }
  size_t write(const void* data, size_t count) { return file ? std::fwrite(data, 1, count, file.get()) : 0; }
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
  HalFile open(const char* path) { return HalFile(resolve(path)); }
  bool remove(const char* path) {
    ++mutations;
    return std::remove(resolve(path).c_str()) == 0;
  }
  bool rename(const char* from, const char* to) {
    ++mutations;
    return std::rename(resolve(from).c_str(), resolve(to).c_str()) == 0;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    ++mutations;
    file = HalFile(resolve(path), "wb");
    return bool(file);
  }
};
#define Storage HalStorage::getInstance()
