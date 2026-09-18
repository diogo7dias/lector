#pragma once
#include <WString.h>

#include <string>
#include <string_view>
#include <vector>

namespace FsHelpers {

std::string decodeUriEscapes(const std::string& path);

std::string normalisePath(const std::string& path);

// Numeric-aware, case-insensitive comparison ("2" < "10"). Returns true when str1 orders
// before str2. Same ordering sortFileList applies within the file/directory groups.
bool naturalLess(const std::string& str1, const std::string& str2);
// Same ordering on NUL-terminated names, so callers holding their names in a
// character arena can sort without materialising a std::string per comparison.
bool naturalLessC(const char* s1, const char* s2);
// Full file-list ordering: directories first, then naturalLessC. Empty names are
// treated as files.
bool fileListLessC(const char* s1, const char* s2);

void sortFileList(std::vector<std::string>& strs);

/**
 * Check if the given filename ends with the specified extension (case-insensitive).
 */
bool checkFileExtension(std::string_view fileName, const char* extension);
inline bool checkFileExtension(const String& fileName, const char* extension) {
  return checkFileExtension(std::string_view{fileName.c_str(), fileName.length()}, extension);
}

// Check for either .jpg or .jpeg extension (case-insensitive)
bool hasJpgExtension(std::string_view fileName);
inline bool hasJpgExtension(const String& fileName) {
  return hasJpgExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .png extension (case-insensitive)
bool hasPngExtension(std::string_view fileName);
inline bool hasPngExtension(const String& fileName) {
  return hasPngExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .bmp extension (case-insensitive)
bool hasBmpExtension(std::string_view fileName);

// Check for .gif extension (case-insensitive)
bool hasGifExtension(std::string_view fileName);
inline bool hasGifExtension(const String& fileName) {
  return hasGifExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .epub extension (case-insensitive)
bool hasEpubExtension(std::string_view fileName);
inline bool hasEpubExtension(const String& fileName) {
  return hasEpubExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for either .xtc or .xtch extension (case-insensitive)
bool hasXtcExtension(std::string_view fileName);

// Check for .txt extension (case-insensitive)
bool hasTxtExtension(std::string_view fileName);
inline bool hasTxtExtension(const String& fileName) {
  return hasTxtExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .md extension (case-insensitive)
bool hasMarkdownExtension(std::string_view fileName);

// Check for .css extension (case-insensitive)
bool hasCssExtension(std::string_view fileName);
inline bool hasCssExtension(const String& fileName) {
  return hasCssExtension(std::string_view{fileName.c_str(), fileName.length()});
}
std::string extractFolderPath(const std::string& filePath);

// The suffix a file being received is written under until it is whole.
//
// Every book scanner on the device, and the font registry, picks files by
// extension, so a name ending in ".part" is invisible to all of them: a
// half-arrived book can sit on the card without appearing in the library and
// failing to open. The final name is taken only once the bytes are verified.
inline constexpr const char* PARTIAL_SUFFIX = ".part";

// "/books/Book.epub" -> "/books/Book.epub.part".
std::string partialPathFor(std::string_view finalPath);

// "/books/Book.epub.part" -> "/books/Book.epub". The path unchanged when it does
// not end in the suffix.
std::string finalPathForPartial(std::string_view partialPath);

// Check for the ".part" staging extension (case-insensitive).
bool hasPartialExtension(std::string_view fileName);
inline bool hasPartialExtension(const String& fileName) {
  return hasPartialExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Rejects an empty component, one containing '/' or '\\', or the exact components
// "." and "..", so a single filename/folder-name argument can never be used to
// escape the directory it is placed into. Names like "volume..2.epub" or
// "notes...txt" that merely contain ".." are accepted.
bool isSafePathComponent(std::string_view name);
inline bool isSafePathComponent(const String& name) {
  return isSafePathComponent(std::string_view{name.c_str(), name.length()});
}

/**
 * Sanitize a filename/path component for FAT32 in a caller-provided buffer.
 * Replaces invalid path characters, spaces, and control characters with '-'.
 */
void sanitizePathComponentForFat32(const char* input, char* output, size_t maxLen);

}  // namespace FsHelpers
