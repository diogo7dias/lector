#include "ReaderPrefs.h"

#include <HalStorage.h>

#include <cstring>

#include "CrossPointSettings.h"

ReaderPrefs ReaderPrefs::fromGlobal() {
  ReaderPrefs p;
  // Every uint8_t field, straight off the three lists in ReaderLookFields.h.
#define CP_FROM_GLOBAL(name) p.name = SETTINGS.name;
  READER_LOOK_SCREEN_FIELDS(CP_FROM_GLOBAL)
  READER_LOOK_BOOK_FIELDS(CP_FROM_GLOBAL)
#undef CP_FROM_GLOBAL
#define CP_FROM_GLOBAL_SB(prefsName, settingsName, blockName) p.prefsName = SETTINGS.settingsName;
  READER_STATUS_BAR_FIELDS(CP_FROM_GLOBAL_SB)
#undef CP_FROM_GLOBAL_SB
  // Zero-pad then copy so the trailing bytes are canonical for whole-blob memcmp.
  std::memset(p.sdFontFamilyName, 0, sizeof(p.sdFontFamilyName));
  std::strncpy(p.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(p.sdFontFamilyName) - 1);
  return p;
}

// Device (SD) serialization. Byte counts are checked so a truncated write/read
// fails cleanly (build is -fno-exceptions; a short read must never be treated as
// a valid record). HalFile::write returns bytes written; HalFile::read returns
// bytes read (int).
bool writeReaderPrefs(HalFile& out, const ReaderPrefs& p) {
  const uint8_t ver = ReaderPrefs::VERSION;
  if (out.write(&ver, 1) != 1) return false;
  return out.write(reinterpret_cast<const uint8_t*>(&p), sizeof(ReaderPrefs)) == sizeof(ReaderPrefs);
}

bool readReaderPrefs(HalFile& in, ReaderPrefs& p, bool* migrated, uint8_t* fromVersion) {
  if (migrated) *migrated = false;
  uint8_t ver = 0;
  if (in.read(&ver, 1) != 1) return false;
  if (fromVersion) *fromVersion = ver;
  // v5 through v9 are read and upgraded rather than discarded, which would silently drop
  // every per-book override the first time this build runs. Each older layout is a strict
  // prefix of the current struct, so a record is read at its own shorter length and every
  // field appended since keeps its constructed default.
  const size_t want = readerPrefsRecordSize(ver);
  if (want == 0) return false;
  ReaderPrefs tmp;
  bool interimV11 = false;
  if (ver == 11) {
    // Two formats were both stamped 11 and differ only in length — see unshiftInterimV11.
    uint8_t record[READER_PREFS_INTERIM_V11_SIZE] = {};
    const int got = in.read(record, READER_PREFS_INTERIM_V11_SIZE);
    if (got < static_cast<int>(READER_PREFS_V11_SIZE)) return false;
    interimV11 = got == static_cast<int>(READER_PREFS_INTERIM_V11_SIZE);
    if (interimV11) {
      unshiftInterimV11(record, tmp);
    } else {
      std::memcpy(&tmp, record, READER_PREFS_V11_SIZE);
    }
  } else if (in.read(reinterpret_cast<uint8_t*>(&tmp), want) != static_cast<int>(want)) {
    return false;
  }
  migrateReaderPrefsFields(ver, tmp, interimV11);
  if (ver < ReaderPrefs::VERSION && migrated) *migrated = true;
  p = tmp;
  return true;
}
