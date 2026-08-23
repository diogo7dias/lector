#include "FontManifestParser.h"

#include <cstdlib>
#include <cstring>

#include "Memory.h"

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

uint32_t parseUnsigned(const char* value, size_t len) {
  char buf[24];
  safeCopy(buf, sizeof(buf), value, len);
  return static_cast<uint32_t>(strtoul(buf, nullptr, 10));
}

// Vectors grow in steps rather than one element at a time: each step is a single
// probed allocation instead of a doubling nobody checked.
constexpr size_t FAMILY_GROWTH_STEP = 16;
constexpr size_t FILE_GROWTH_STEP = 8;

}  // namespace

FontManifestParser::FontManifestParser()
    : parser_(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                            sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void FontManifestParser::reset() {
  parser_.reset();
  pos_ = Pos::TOP;
  key_ = Key::NONE;
  skipDepth_ = 0;
  rootOpen_ = false;
  rootClosed_ = false;
  version_ = 0;
  baseUrl_[0] = '\0';
  families_.clear();
  families_.shrink_to_fit();
  currentFamily_ = FontManifestFamily{};
  currentFile_ = FontManifestFile{};
  currentFileHasCrc32_ = false;
  error_ = false;
  tooLarge_ = false;
  outOfMemory_ = false;
}

void FontManifestParser::feed(const char* data, size_t len) {
  if (error_) return;
  parser_.feed(data, len);
  if (parser_.hasError()) error_ = true;
}

void FontManifestParser::finish() {
  if (!rootClosed_) error_ = true;
}

void FontManifestParser::beginFamily() {
  currentFamily_ = FontManifestFamily{};
  currentFile_ = FontManifestFile{};
  currentFileHasCrc32_ = false;
}

void FontManifestParser::commitFamily() {
  if (families_.size() >= MAX_FAMILIES) {
    tooLarge_ = true;
    error_ = true;
    return;
  }
  if (families_.size() == families_.capacity()) {
    const size_t next = families_.size() + FAMILY_GROWTH_STEP;
    if (!reserveNoThrow(families_, next)) {
      outOfMemory_ = true;
      error_ = true;
      return;
    }
  }
  families_.push_back(std::move(currentFamily_));
  currentFamily_ = FontManifestFamily{};
}

void FontManifestParser::beginFile() {
  currentFile_ = FontManifestFile{};
  currentFileHasCrc32_ = false;
}

void FontManifestParser::commitFile() {
  // A file the reader cannot verify is a file it must not install, so a missing
  // crc32 fails the whole manifest rather than the one entry.
  if (!currentFileHasCrc32_) {
    error_ = true;
    return;
  }
  auto& files = currentFamily_.files;
  if (files.size() >= MAX_FILES_PER_FAMILY) {
    tooLarge_ = true;
    error_ = true;
    return;
  }
  if (files.size() == files.capacity()) {
    const size_t next = files.size() + FILE_GROWTH_STEP;
    if (!reserveNoThrow(files, next)) {
      outOfMemory_ = true;
      error_ = true;
      return;
    }
  }
  currentFamily_.totalSize += currentFile_.size;
  files.push_back(currentFile_);
  currentFile_ = FontManifestFile{};
  currentFileHasCrc32_ = false;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void FontManifestParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<FontManifestParser*>(ctx);
  if (self->error_ || self->skipDepth_ > 0) return;

  self->key_ = Key::NONE;
  switch (self->pos_) {
    case Pos::TOP:
      if (len == 7 && memcmp(key, "version", 7) == 0)
        self->key_ = Key::VERSION;
      else if (len == 7 && memcmp(key, "baseUrl", 7) == 0)
        self->key_ = Key::BASE_URL;
      else if (len == 8 && memcmp(key, "families", 8) == 0)
        self->key_ = Key::FAMILIES;
      break;
    case Pos::FAMILY:
      if (len == 4 && memcmp(key, "name", 4) == 0)
        self->key_ = Key::FAMILY_NAME;
      else if (len == 11 && memcmp(key, "description", 11) == 0)
        self->key_ = Key::FAMILY_DESCRIPTION;
      else if (len == 5 && memcmp(key, "files", 5) == 0)
        self->key_ = Key::FAMILY_FILES;
      break;
    case Pos::FILE:
      if (len == 4 && memcmp(key, "name", 4) == 0)
        self->key_ = Key::FILE_NAME;
      else if (len == 4 && memcmp(key, "size", 4) == 0)
        self->key_ = Key::FILE_SIZE;
      else if (len == 5 && memcmp(key, "crc32", 5) == 0)
        self->key_ = Key::FILE_CRC32;
      break;
    default:
      break;
  }
}

void FontManifestParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<FontManifestParser*>(ctx);
  if (self->error_ || self->skipDepth_ > 0) return;

  switch (self->key_) {
    case Key::BASE_URL:
      safeCopy(self->baseUrl_, sizeof(self->baseUrl_), value, len);
      break;
    case Key::FAMILY_NAME:
      safeCopy(self->currentFamily_.name, sizeof(self->currentFamily_.name), value, len);
      break;
    case Key::FAMILY_DESCRIPTION:
      safeCopy(self->currentFamily_.description, sizeof(self->currentFamily_.description), value, len);
      break;
    case Key::FILE_NAME:
      safeCopy(self->currentFile_.name, sizeof(self->currentFile_.name), value, len);
      break;
    default:
      break;
  }
  self->key_ = Key::NONE;
}

void FontManifestParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<FontManifestParser*>(ctx);
  if (self->error_ || self->skipDepth_ > 0) return;

  switch (self->key_) {
    case Key::VERSION:
      self->version_ = static_cast<int>(parseUnsigned(value, len));
      break;
    case Key::FILE_SIZE:
      self->currentFile_.size = parseUnsigned(value, len);
      break;
    case Key::FILE_CRC32:
      self->currentFile_.crc32 = parseUnsigned(value, len);
      self->currentFileHasCrc32_ = true;
      break;
    default:
      break;
  }
  self->key_ = Key::NONE;
}

void FontManifestParser::sOnBool(void* ctx, bool) { static_cast<FontManifestParser*>(ctx)->key_ = Key::NONE; }

void FontManifestParser::sOnNull(void* ctx) { static_cast<FontManifestParser*>(ctx)->key_ = Key::NONE; }

void FontManifestParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<FontManifestParser*>(ctx);
  if (self->error_) return;

  if (self->skipDepth_ > 0) {
    self->skipDepth_++;
    return;
  }
  if (!self->rootOpen_) {
    self->rootOpen_ = true;
    self->key_ = Key::NONE;
    return;
  }
  switch (self->pos_) {
    case Pos::FAMILIES:
      self->beginFamily();
      self->pos_ = Pos::FAMILY;
      break;
    case Pos::FILES:
      self->beginFile();
      self->pos_ = Pos::FILE;
      break;
    default:
      // An object under a key nothing reads (scriptGroups, or whatever the manifest
      // grows next). Swallow it whole.
      self->skipDepth_ = 1;
      break;
  }
  self->key_ = Key::NONE;
}

void FontManifestParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<FontManifestParser*>(ctx);
  if (self->error_) return;

  if (self->skipDepth_ > 0) {
    self->skipDepth_--;
    return;
  }
  switch (self->pos_) {
    case Pos::FILE:
      self->commitFile();
      self->pos_ = Pos::FILES;
      break;
    case Pos::FAMILY:
      self->commitFamily();
      self->pos_ = Pos::FAMILIES;
      break;
    case Pos::TOP:
      if (self->rootOpen_) self->rootClosed_ = true;
      break;
    default:
      break;
  }
  self->key_ = Key::NONE;
}

void FontManifestParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<FontManifestParser*>(ctx);
  if (self->error_) return;

  if (self->skipDepth_ > 0) {
    self->skipDepth_++;
    return;
  }
  if (self->pos_ == Pos::TOP && self->key_ == Key::FAMILIES) {
    self->pos_ = Pos::FAMILIES;
  } else if (self->pos_ == Pos::FAMILY && self->key_ == Key::FAMILY_FILES) {
    self->pos_ = Pos::FILES;
  } else {
    // styles, scripts, and anything else that arrives later.
    self->skipDepth_ = 1;
  }
  self->key_ = Key::NONE;
}

void FontManifestParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<FontManifestParser*>(ctx);
  if (self->error_) return;

  if (self->skipDepth_ > 0) {
    self->skipDepth_--;
    return;
  }
  switch (self->pos_) {
    case Pos::FILES:
      self->pos_ = Pos::FAMILY;
      break;
    case Pos::FAMILIES:
      self->pos_ = Pos::TOP;
      break;
    default:
      break;
  }
  self->key_ = Key::NONE;
}
