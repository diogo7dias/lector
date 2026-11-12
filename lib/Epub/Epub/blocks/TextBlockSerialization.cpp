// TextBlock on-disk layout: written into section.bin, read back on page load.
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <cstring>

#include "TextBlock.h"

bool TextBlock::serialize(HalFile& file) const {
  if (!isValid) {
    LOG_ERR("TXB", "Serialization failed: invalid block");
    return false;
  }

  // Word data: scalars, then the arena verbatim -- its in-memory layout is
  // exactly the on-disk layout (see TextBlock.h), so one write covers all
  // per-word arrays and the text blob.
  serialization::writePod(file, numWords);
  serialization::writePod(file, static_cast<uint8_t>(focusPresent ? 1 : 0));
  serialization::writePod(file, static_cast<uint8_t>(guideDotsPresent ? 1 : 0));
  serialization::writePod(file, textBytes);
  if (numWords > 0) {
    const size_t size = arenaSize(numWords, focusPresent, guideDotsPresent, textBytes);
    if (file.write(arena.get(), size) != size) {
      LOG_ERR("TXB", "Serialization failed: arena write (%u bytes)", static_cast<uint32_t>(size));
      return false;
    }
  }

  // Ruby text data
  for (size_t i = 0; i < numWords; i++) {
    serialization::writeString(file, (i < rubyTexts.size()) ? rubyTexts[i] : std::string());
  }

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);
  serialization::writePod(file, blockStyle.isRtl);
  serialization::writePod(file, blockStyle.directionDefined);
  serialization::writePod(file, letterSpacing);
  serialization::writePod(file, blockStyle.fontId);

  // Per-word fonts: a flag, then the font table and one slot per word.
  const uint8_t hasWordFonts = wordFontData ? 1 : 0;
  serialization::writePod(file, hasWordFonts);
  if (hasWordFonts) {
    const size_t size = sizeof(int32_t) * MAX_WORD_FONTS + numWords;
    if (file.write(wordFontData.get(), size) != size) {
      LOG_ERR("TXB", "Serialization failed: word fonts");
      return false;
    }
  }

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(HalFile& file) {
  uint16_t wc;
  uint8_t hasFocus;
  uint8_t hasGuideDots;
  uint16_t textBytes;
  serialization::readPod(file, wc);
  serialization::readPod(file, hasFocus);
  serialization::readPod(file, hasGuideDots);
  serialization::readPod(file, textBytes);

  // Sanity checks: cap the arena allocation and reject impossible geometry
  // (every word carries at least its NUL terminator).
  if (wc > 10000) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }
  if (hasFocus > 1 || hasGuideDots > 1) {
    LOG_ERR("TXB", "Deserialization failed: bad flag bytes %u/%u", hasFocus, hasGuideDots);
    return nullptr;
  }
  if ((wc == 0 && textBytes != 0) || (wc > 0 && textBytes < wc)) {
    LOG_ERR("TXB", "Deserialization failed: bad text size %u for %u words", textBytes, wc);
    return nullptr;
  }

  std::unique_ptr<TextBlock> block(new (std::nothrow) TextBlock());
  if (!block) {
    LOG_ERR("TXB", "OOM: TextBlock");
    return nullptr;
  }
  block->numWords = wc;
  block->textBytes = textBytes;
  block->focusPresent = hasFocus != 0;
  block->guideDotsPresent = hasGuideDots != 0;

  if (wc > 0) {
    const size_t size = arenaSize(wc, block->focusPresent, block->guideDotsPresent, textBytes);
    block->arena = makeUniqueNoThrow<uint8_t[]>(size);
    if (!block->arena) {
      LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
      return nullptr;
    }
    if (file.read(block->arena.get(), size) != size) {
      LOG_ERR("TXB", "Deserialization failed: arena read (%u bytes)", static_cast<uint32_t>(size));
      return nullptr;
    }
    block->bindArenaPointers();

    // Validate offsets before anything dereferences wordText(): offset 0 first,
    // strictly increasing, in bounds, and every word NUL-terminated (word i ends
    // at the byte before offset i+1; the last word at the last text byte).
    const uint16_t* textOff = block->textOffArr;
    const char* text = block->textArr;
    if (textOff[0] != 0 || text[textBytes - 1] != '\0') {
      LOG_ERR("TXB", "Deserialization failed: corrupt text layout");
      return nullptr;
    }
    for (uint16_t i = 1; i < wc; i++) {
      if (textOff[i] <= textOff[i - 1] || textOff[i] >= textBytes || text[textOff[i] - 1] != '\0') {
        LOG_ERR("TXB", "Deserialization failed: corrupt word offset %u", i);
        return nullptr;
      }
    }
  }

  // Ruby text data. Ruby is a CJK feature, so for nearly every book every entry here
  // is the empty string. Materializing the vector regardless costs wordCount * 24 bytes
  // (sizeof(std::string)) plus a heap block per line, held for as long as the page is
  // resident -- several KB of DRAM on a full page, none of it ever read. An empty
  // rubyTexts is already the "no ruby" representation: hasRuby() reports false and every
  // other reader is guarded by `i < rubyTexts.size()`, so allocate lazily and only once a
  // non-empty annotation actually shows up. Upstream #2772.
  //
  // `scratch` is reused across words: readString() resizes it to the incoming length and
  // overwrites every byte, so a moved-from value carries nothing into the next iteration.
  std::string scratch;
  for (uint16_t i = 0; i < wc; i++) {
    // Every field after this one is positional: a refused string would misalign them all.
    if (!serialization::readString(file, scratch)) {
      LOG_ERR("TXB", "Deserialization failed: ruby text %u", i);
      return nullptr;
    }
    if (scratch.empty()) continue;
    if (block->rubyTexts.empty()) {
      block->rubyTexts.resize(wc);
    }
    block->rubyTexts[i] = std::move(scratch);
  }

  // Style (alignment + margins/padding/indent)
  BlockStyle& blockStyle = block->blockStyle;
  serialization::readPod(file, blockStyle.alignment);
  serialization::readPod(file, blockStyle.textAlignDefined);
  serialization::readPod(file, blockStyle.marginTop);
  serialization::readPod(file, blockStyle.marginBottom);
  serialization::readPod(file, blockStyle.marginLeft);
  serialization::readPod(file, blockStyle.marginRight);
  serialization::readPod(file, blockStyle.paddingTop);
  serialization::readPod(file, blockStyle.paddingBottom);
  serialization::readPod(file, blockStyle.paddingLeft);
  serialization::readPod(file, blockStyle.paddingRight);
  serialization::readPod(file, blockStyle.textIndent);
  serialization::readPod(file, blockStyle.textIndentDefined);
  serialization::readPod(file, blockStyle.isRtl);
  serialization::readPod(file, blockStyle.directionDefined);
  serialization::readPod(file, block->letterSpacing);
  serialization::readPod(file, blockStyle.fontId);

  uint8_t hasWordFonts = 0;
  serialization::readPod(file, hasWordFonts);
  if (hasWordFonts > 1) {
    LOG_ERR("TXB", "Deserialization failed: bad word font flag %u", hasWordFonts);
    return nullptr;
  }
  if (hasWordFonts) {
    const size_t size = sizeof(int32_t) * MAX_WORD_FONTS + wc;
    block->wordFontData = makeUniqueNoThrow<uint8_t[]>(size);
    if (!block->wordFontData || file.read(block->wordFontData.get(), size) != size) {
      LOG_ERR("TXB", "Deserialization failed: word fonts");
      return nullptr;
    }
    const uint8_t* slots = block->wordFontData.get() + sizeof(int32_t) * MAX_WORD_FONTS;
    for (uint16_t i = 0; i < wc; ++i) {
      const uint8_t slot = slots[i] & ~SMALL_CAPS_SLOT_FLAG;
      int32_t font = 1;
      if (slot > 0 && slot <= MAX_WORD_FONTS) {
        memcpy(&font, block->wordFontData.get() + sizeof(int32_t) * (slot - 1), sizeof(font));
      }
      if (slot > MAX_WORD_FONTS || font == 0) {
        LOG_ERR("TXB", "Deserialization failed: bad word font slot %u", slots[i]);
        return nullptr;
      }
    }
  }

  return block;
}
