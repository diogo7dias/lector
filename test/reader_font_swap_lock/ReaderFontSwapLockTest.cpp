// Source audit: the reader swaps its SD font only under the render lock.
//
// SdCardFontSystem::ensureLoadedFor() deletes the resident SdCardFont before it loads the
// next one, and the render task lays out and draws with that font while it holds the
// RenderLock. A swap made from the loop task without the lock (a pinch, the size row in the
// light panel, a preset adopted or reset) could free the font under a running render. The
// reader keeps one place that swaps for a look change, reloadForReaderPrefsChange(), which
// does it under its own lock; onEnter() loads the book's font before anything renders it.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr const char* kSwap = "sdFontSystem.ensureLoadedFor(";

std::string readFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// The text of a function from its signature to the next top-level closing brace.
std::string bodyOf(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return {};
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

int countOf(const std::string& haystack, const std::string& needle) {
  int count = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) ++count;
  return count;
}

}  // namespace

TEST(ReaderFontSwapLock, EveryLookChangeSwapsTheFontInsideTheRelayoutLock) {
  int swaps = 0;
  std::string reload;
  std::string onEnter;
  for (const auto& entry : std::filesystem::directory_iterator(READER_SOURCE_DIR)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("EpubReaderActivity", 0) != 0 || entry.path().extension() != ".cpp") continue;
    const std::string source = readFile(entry.path());
    swaps += countOf(source, kSwap);
    if (reload.empty()) reload = bodyOf(source, "void EpubReaderActivity::reloadForReaderPrefsChange()");
    if (onEnter.empty()) onEnter = bodyOf(source, "void EpubReaderActivity::onEnter()");
  }
  ASSERT_FALSE(reload.empty()) << "reloadForReaderPrefsChange() not found";
  ASSERT_FALSE(onEnter.empty()) << "onEnter() not found";

  const size_t lock = reload.find("RenderLock lock");
  const size_t swap = reload.find(kSwap);
  ASSERT_NE(lock, std::string::npos) << "the relayout no longer takes the render lock";
  ASSERT_NE(swap, std::string::npos) << "the relayout no longer makes prefs_'s font resident";
  EXPECT_LT(lock, swap) << "the font swap runs before the render lock is taken";

  EXPECT_EQ(countOf(onEnter, kSwap), 1);
  EXPECT_EQ(swaps, 2) << "a font swap outside reloadForReaderPrefsChange() and onEnter(): it runs on the "
                         "loop task without the render lock, so route it through the relayout instead";
}
