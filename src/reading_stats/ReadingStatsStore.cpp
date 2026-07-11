#include "ReadingStatsStore.h"

namespace reading_stats {

DecodeResult ReadingStatsStore::readOne(const std::string& path, ReadingStatsData& stats, bool& found) {
  found = files_.exists(path);
  if (!found) return DecodeResult::Invalid;
  ReadingStatsCodec::Encoded bytes{};
  size_t size = 0;
  if (!files_.read(path, bytes.data(), bytes.size(), size)) return DecodeResult::Invalid;
  return ReadingStatsCodec::decode(bytes.data(), size, stats);
}

StatsLoadResult ReadingStatsStore::load(const std::string& path, ReadingStatsData& stats) {
  stats = {};
  bool found = false;
  const DecodeResult primary = readOne(path, stats, found);
  if (primary == DecodeResult::Ok) return StatsLoadResult::Ok;
  if (primary == DecodeResult::NewerVersion) {
    blockedNewerPath_ = path;
    stats = {};
    return StatsLoadResult::NewerVersion;
  }

  ReadingStatsData backup;
  bool backupFound = false;
  const DecodeResult backupResult = readOne(path + ".bak", backup, backupFound);
  if (backupResult == DecodeResult::Ok) {
    stats = backup;
    return StatsLoadResult::RecoveredBackup;
  }
  if (backupResult == DecodeResult::NewerVersion) {
    blockedNewerPath_ = path;
    return StatsLoadResult::NewerVersion;
  }
  return found || backupFound ? StatsLoadResult::Invalid : StatsLoadResult::Missing;
}

bool ReadingStatsStore::save(const std::string& path, const ReadingStatsData& stats) {
  if (blockedNewerPath_ == path) return false;

  const std::string tempPath = path + ".tmp";
  const std::string backupPath = path + ".bak";
  if (files_.exists(tempPath) && !files_.remove(tempPath)) return false;

  const ReadingStatsCodec::Encoded bytes = ReadingStatsCodec::encode(stats);
  if (!files_.write(tempPath, bytes.data(), bytes.size())) return false;

  ReadingStatsData verify;
  bool tempFound = false;
  if (readOne(tempPath, verify, tempFound) != DecodeResult::Ok || !(verify == stats)) {
    files_.remove(tempPath);
    return false;
  }

  if (files_.exists(backupPath) && !files_.remove(backupPath)) {
    files_.remove(tempPath);
    return false;
  }
  const bool hadOriginal = files_.exists(path);
  if (hadOriginal && !files_.rename(path, backupPath)) {
    files_.remove(tempPath);
    return false;
  }
  if (!files_.rename(tempPath, path)) {
    if (hadOriginal && files_.exists(backupPath) && !files_.exists(path)) files_.rename(backupPath, path);
    files_.remove(tempPath);
    return false;
  }
  return true;
}

bool ReadingStatsStore::reset(const std::string& path) {
  if (blockedNewerPath_ == path) return false;
  if (files_.exists(path) && !files_.remove(path)) return false;
  if (files_.exists(path + ".bak") && !files_.remove(path + ".bak")) return false;
  if (files_.exists(path + ".tmp") && !files_.remove(path + ".tmp")) return false;
  return save(path, {});
}

}  // namespace reading_stats
