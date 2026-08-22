#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "util/BrowserRowFile.h"

using browser_row::cardPath;
using browser_row::rowFile;

namespace {

// Records what the row asked the queue, so a test can assert on the key itself and on
// the rows that must never ask at all.
struct FakeQueue {
  std::vector<std::string> asked;
  std::string answer;

  std::string operator()(const std::string& key) {
    asked.push_back(key);
    return answer;
  }
};

}  // namespace

TEST(CardPath, IsTheFolderAndTheNameOnTheCard) { EXPECT_EQ(cardPath("/sleep", "wave.pxc"), "/sleep/wave.pxc"); }

// The open path appends a slash to basepath before opening a book and that state survives
// the return, so every caller has to survive it too.
TEST(CardPath, TrimsATrailingSlashOnTheFolder) { EXPECT_EQ(cardPath("/sleep/", "wave.pxc"), "/sleep/wave.pxc"); }

TEST(CardPath, TrimsEveryTrailingSlash) { EXPECT_EQ(cardPath("/sleep//", "wave.pxc"), "/sleep/wave.pxc"); }

TEST(CardPath, AtTheRoot) { EXPECT_EQ(cardPath("/", "wave.pxc"), "/wave.pxc"); }

TEST(RowFile, NothingQueuedLeavesTheCardName) {
  FakeQueue queue;
  EXPECT_EQ(rowFile("/sleep", "wave.pxc", std::ref(queue)), "wave.pxc");
  ASSERT_EQ(queue.asked.size(), 1u);
}

// The bug this file exists for: the queue is keyed by the path the card holds, extension
// and all. Keying it on the row's label found nothing and the favorite never showed.
TEST(RowFile, AsksTheQueueForTheRawCardPath) {
  FakeQueue queue;
  rowFile("/sleep/", "wave.pxc", std::ref(queue));
  ASSERT_EQ(queue.asked.size(), 1u);
  EXPECT_EQ(queue.asked[0], "/sleep/wave.pxc");
}

// The queue answers a whole path; the row draws a name.
TEST(RowFile, AQueuedFavoriteDrawsAsItsBasename) {
  FakeQueue queue;
  queue.answer = "/sleep/wave_F.pxc";
  EXPECT_EQ(rowFile("/sleep", "wave.pxc", std::ref(queue)), "wave_F.pxc");
}

TEST(RowFile, AQueuedUnfavoriteDrawsAsItsBasename) {
  FakeQueue queue;
  queue.answer = "/sleep/wave.pxc";
  EXPECT_EQ(rowFile("/sleep", "wave_F.pxc", std::ref(queue)), "wave.pxc");
}

TEST(RowFile, AQueuedNameAtTheRoot) {
  FakeQueue queue;
  queue.answer = "/wave_F.pxc";
  EXPECT_EQ(rowFile("/", "wave.pxc", std::ref(queue)), "wave_F.pxc");
}

TEST(RowFile, BmpIsAWallpaperToo) {
  FakeQueue queue;
  queue.answer = "/sleep/wave_F.BMP";
  EXPECT_EQ(rowFile("/sleep", "wave.BMP", std::ref(queue)), "wave_F.BMP");
}

// Only a wallpaper can carry a queued rename. Everything else must skip the lookup: it
// takes the queue's mutex and copies the pending jobs, once per row, over a folder that
// can hold thousands.
TEST(RowFile, AFolderRowNeverAsksTheQueue) {
  FakeQueue queue;
  EXPECT_EQ(rowFile("/sleep", "sub/", std::ref(queue)), "sub/");
  EXPECT_TRUE(queue.asked.empty());
}

TEST(RowFile, ABookRowNeverAsksTheQueue) {
  FakeQueue queue;
  EXPECT_EQ(rowFile("/books", "novel.epub", std::ref(queue)), "novel.epub");
  EXPECT_TRUE(queue.asked.empty());
}
