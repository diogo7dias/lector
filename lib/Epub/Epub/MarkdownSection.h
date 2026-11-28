#pragma once

#include <Epub/ReaderRenderSpec.h>
#include <Epub/parsers/MarkdownParser.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GfxRenderer;
class Page;

// Binary section.bin cache for Markdown documents: stores serialized Page records,
// a page-offset LUT, and extracted # / ## TOC entries keyed by ReaderRenderSpec + fileSize.
class MarkdownSection {
 public:
  explicit MarkdownSection(std::string cacheDir);

  bool load(const ReaderRenderSpec& spec, uint32_t fileSize);
  bool build(const std::string& mdPath, GfxRenderer& renderer, const ReaderRenderSpec& spec, uint32_t fileSize);
  std::unique_ptr<Page> loadPage(int pageIndex) const;

  uint16_t pageCount() const { return pageCount_; }
  const std::vector<MarkdownTocEntry>& toc() const { return toc_; }
  const std::string& cacheFilePath() const { return filePath_; }

 private:
  std::string filePath_;
  uint16_t pageCount_ = 0;
  uint32_t pageLutOffset_ = 0;
  std::vector<MarkdownTocEntry> toc_;
};
