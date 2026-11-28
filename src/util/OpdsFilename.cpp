#include "OpdsFilename.h"

#include "StringUtils.h"

std::string opdsBookFilename(const std::string& author, const std::string& title, OpdsFilenameFormat format,
                             const char* extension) {
  std::string base;
  switch (format) {
    case OpdsFilenameFormat::TitleAuthor:
      base = author.empty() ? title : title + " - " + author;
      break;
    case OpdsFilenameFormat::TitleOnly:
      base = title;
      break;
    case OpdsFilenameFormat::AuthorTitle:
    default:
      base = author.empty() ? title : author + " - " + title;
      break;
  }
  const char* ext = (extension && extension[0] != '\0') ? extension : ".epub";
  // sanitizeFilename caps at 100 bytes and never returns empty (falls back to
  // "book"); the extension is appended after so it is never truncated.
  return StringUtils::sanitizeFilename(base) + ext;
}

std::string opdsTargetPath(const char* downloadFolder, const std::string& author, const std::string& title,
                           OpdsFilenameFormat format, const char* extension) {
  std::string path;
  path.reserve(96);
  if (downloadFolder && downloadFolder[0] != '\0') path += downloadFolder;
  path += '/';
  path += opdsBookFilename(author, title, format, extension);
  return path;
}
