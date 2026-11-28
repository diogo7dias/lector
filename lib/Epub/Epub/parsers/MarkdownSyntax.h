#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace markdown_syntax {

inline bool isAsciiSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

inline std::string_view trimLeft(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  return s;
}

inline std::string_view trimRight(std::string_view s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// Parse ATX heading ("# Title" .. "###### Title"). Returns level 1..6, or 0 if not a heading.
inline uint8_t parseHeadingPrefix(std::string_view line, std::string_view& outText) {
  size_t hashes = 0;
  while (hashes < line.size() && hashes < 6 && line[hashes] == '#') ++hashes;
  if (hashes == 0 || hashes >= line.size() || line[hashes] != ' ') return 0;
  std::string_view body = trimRight(trimLeft(line.substr(hashes + 1)));
  while (!body.empty() && body.back() == '#') body.remove_suffix(1);
  body = trimRight(body);
  if (body.empty()) return 0;
  outText = body;
  return static_cast<uint8_t>(hashes);
}

// Parse fenced code opening ("```", "```cpp", "~~~"). Returns fence length (>= 3) or 0.
inline uint8_t parseFenceOpening(std::string_view line, char& outFenceChar) {
  const std::string_view trimmed = trimLeft(line);
  if (trimmed.size() < 3) return 0;
  const char ch = trimmed[0];
  if (ch != '`' && ch != '~') return 0;
  size_t count = 0;
  while (count < trimmed.size() && trimmed[count] == ch) ++count;
  if (count < 3) return 0;
  // Backtick info strings cannot contain backticks (CommonMark 4.5).
  if (ch == '`' && trimmed.substr(count).find('`') != std::string_view::npos) return 0;
  outFenceChar = ch;
  return static_cast<uint8_t>(count > 255 ? 255 : count);
}

// True when `line` closes a code fence opened with `fenceChar` repeated `fenceLen` times.
inline bool isFenceClosing(std::string_view line, const char fenceChar, const uint8_t fenceLen) {
  const std::string_view trimmed = trimRight(trimLeft(line));
  if (trimmed.size() < fenceLen) return false;
  for (const char c : trimmed) {
    if (c != fenceChar) return false;
  }
  return true;
}

// Parse unordered list item ("- text", "* text", "+ text").
inline bool parseBulletPrefix(std::string_view line, std::string_view& outText) {
  const std::string_view trimmed = trimLeft(line);
  if (trimmed.size() < 2) return false;
  if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ') {
    outText = trimLeft(trimmed.substr(2));
    return !outText.empty();
  }
  return false;
}

// Parse ordered list item ("1. text", "12. text").
inline bool parseOrderedPrefix(std::string_view line, std::string& outMarker, std::string_view& outText) {
  const std::string_view trimmed = trimLeft(line);
  size_t digits = 0;
  while (digits < trimmed.size() && digits < 6 && std::isdigit(static_cast<unsigned char>(trimmed[digits]))) {
    ++digits;
  }
  if (digits == 0 || digits + 1 >= trimmed.size()) return false;
  if (trimmed[digits] != '.' || trimmed[digits + 1] != ' ') return false;
  outText = trimLeft(trimmed.substr(digits + 2));
  if (outText.empty()) return false;
  outMarker.assign(trimmed.data(), digits + 1);
  return true;
}

// Parse link "[label](url)" starting at `pos` (where text[pos] == '[').
inline bool matchInlineLink(std::string_view text, const size_t pos, std::string_view& label, size_t& outEnd) {
  if (pos >= text.size() || text[pos] != '[') return false;
  size_t closeBracket = std::string_view::npos;
  for (size_t i = pos + 1; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      ++i;
      continue;
    }
    if (text[i] == ']') {
      closeBracket = i;
      break;
    }
  }
  if (closeBracket == std::string_view::npos || closeBracket + 1 >= text.size() || text[closeBracket + 1] != '(') {
    return false;
  }
  int parenDepth = 1;
  size_t i = closeBracket + 2;
  for (; i < text.size() && parenDepth > 0; ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      ++i;
      continue;
    }
    if (text[i] == '(')
      ++parenDepth;
    else if (text[i] == ')')
      --parenDepth;
  }
  if (parenDepth != 0) return false;
  label = text.substr(pos + 1, closeBracket - pos - 1);
  outEnd = i;
  return true;
}

// Check if delimiter `delim` has a matching closing occurrence before end of `text`.
inline bool hasClosingDelimiter(std::string_view text, const size_t afterOpen, std::string_view delim) {
  if (afterOpen >= text.size() || isAsciiSpace(text[afterOpen])) return false;
  for (size_t i = afterOpen; i + delim.size() <= text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      ++i;
      continue;
    }
    if (text.substr(i, delim.size()) == delim && i > afterOpen && !isAsciiSpace(text[i - 1])) {
      return true;
    }
  }
  return false;
}

// Find a safe split offset in a long physical line before '\n' so streaming chunks never drop
// bytes, never split a UTF-8 codepoint, and prefer breaking on whitespace outside inline links.
inline size_t findSafeChunkSplit(std::string_view buf) {
  const size_t minSplit = buf.size() / 2;
  for (size_t i = buf.size(); i > minSplit; --i) {
    if (buf[i - 1] == ' ' || buf[i - 1] == '\t') {
      // Avoid splitting right inside an unclosed "[label](url" tail.
      const size_t openBracket = buf.rfind('[', i - 1);
      if (openBracket != std::string_view::npos && openBracket >= minSplit) {
        const size_t closeParen = buf.find(')', openBracket);
        if (closeParen == std::string_view::npos || closeParen >= i) {
          if (openBracket > minSplit / 2) return openBracket;
        }
      }
      return i;
    }
  }
  // No whitespace in the second half (one huge token): step back to a UTF-8 lead byte.
  size_t pos = buf.size();
  while (pos > minSplit && (static_cast<uint8_t>(buf[pos - 1]) & 0xC0) == 0x80) --pos;
  if (pos > 0 && (static_cast<uint8_t>(buf[pos - 1]) & 0xC0) == 0xC0) --pos;
  return pos > 0 ? pos : buf.size();
}

// Strip inline markdown markers (**, *, `, [label](url)) into plain text for TOC titles.
inline std::string stripInlineMarkdown(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  bool bold = false;
  bool italic = false;
  while (i < text.size()) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      out.push_back(text[i + 1]);
      i += 2;
      continue;
    }
    if (text[i] == '`') {
      const size_t close = text.find('`', i + 1);
      if (close != std::string_view::npos) {
        out.append(text.data() + i + 1, close - i - 1);
        i = close + 1;
        continue;
      }
    }
    if (text[i] == '[') {
      std::string_view label;
      size_t endPos = 0;
      if (matchInlineLink(text, i, label, endPos)) {
        out += stripInlineMarkdown(label);
        i = endPos;
        continue;
      }
    }
    if (i + 2 < text.size() && text.substr(i, 3) == "***") {
      if ((bold && italic) || hasClosingDelimiter(text, i + 3, "***")) {
        bold = !bold;
        italic = !italic;
        i += 3;
        continue;
      }
    }
    if (i + 1 < text.size() && text.substr(i, 2) == "**") {
      if (bold || hasClosingDelimiter(text, i + 2, "**")) {
        bold = !bold;
        i += 2;
        continue;
      }
    }
    if (text[i] == '*') {
      if (italic || hasClosingDelimiter(text, i + 1, "*")) {
        italic = !italic;
        i += 1;
        continue;
      }
    }
    out.push_back(text[i++]);
  }
  return out;
}

}  // namespace markdown_syntax
