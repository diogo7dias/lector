// The reader-edit overlay's lifetime, modelled on the host.
//
// While Reader Settings is open on a book, that book's font/margins are overlaid
// onto the LIVE global reader fields, with the true globals parked in a backup
// (CrossPointSettings::beginReaderEditOverlay). Two things must end that overlay
// no matter how the reader is left:
//
//   - the live fields must go back to the true globals, or the next book with no
//     override of its own inherits the previous book's look as if it were global;
//   - readerEditSink_ must be cleared, because it points into the reader activity
//     and saveToFile() calls through it.
//
// applyReaderSettingsEdit() (the result handler) only runs on the POP path. Home
// and sleep REPLACE the activity stack, which destroys the reader WITHOUT running
// any result handler — see ActivityManager::loop, PendingAction::Replace. So the
// end-of-overlay belongs in EpubReaderActivity::onExit(), which every destruction
// path routes through.
//
// CrossPointSettings itself is not host-buildable (Arduino, SD, SPIFFS), so this
// models the exact begin/end/save protocol from CrossPointSettings.cpp:628-691.
#include <gtest/gtest.h>

namespace {

// Stand-in for the reader fields that the overlay swaps. One number is enough to
// tell "the book's look" from "the global look".
using Look = int;

constexpr Look kGlobalLook = 10;
constexpr Look kBookLook = 77;

// The overlay half of CrossPointSettings, kept to the same rules as the real one.
struct Settings {
  Look live = kGlobalLook;  // the live global reader fields
  Look backup = 0;          // true globals, parked while an overlay is active
  bool overlayActive = false;
  const void* sinkCtx = nullptr;  // points into the reader activity

  Look persisted = kGlobalLook;  // what settings.json holds

  void beginReaderEditOverlay(const Look startValues, const void* ctx) {
    if (!overlayActive) {
      backup = live;
      overlayActive = true;
    }
    sinkCtx = ctx;
    live = startValues;
  }

  Look endReaderEditOverlay() {
    const Look edited = live;
    if (overlayActive) {
      live = backup;
      overlayActive = false;
    }
    sinkCtx = nullptr;  // cleared unconditionally: must never outlive the activity
    return edited;
  }

  // saveToFile() persists the GLOBAL values even mid-edit, so a book's look can
  // never become the global one.
  void saveToFile() { persisted = overlayActive ? backup : live; }
};

// The reader, reduced to the part that matters: it owns the overlay's lifetime.
struct Reader {
  Settings& s;
  bool exited = false;

  explicit Reader(Settings& settings) : s(settings) {}

  void openReaderSettings() { s.beginReaderEditOverlay(kBookLook, this); }

  // The fix: every destruction path routes through onExit().
  void onExit() {
    if (s.overlayActive) s.endReaderEditOverlay();
    exited = true;
  }
};

}  // namespace

TEST(ReaderEditOverlayLifetime, LeavingViaBackEndsTheOverlay) {
  Settings s;
  Reader reader(s);
  reader.openReaderSettings();
  ASSERT_TRUE(s.overlayActive);
  ASSERT_EQ(s.live, kBookLook);

  // POP: the result handler runs, then the activity is destroyed.
  s.endReaderEditOverlay();  // applyReaderSettingsEdit()
  reader.onExit();

  EXPECT_FALSE(s.overlayActive);
  EXPECT_EQ(s.live, kGlobalLook);
  EXPECT_EQ(s.sinkCtx, nullptr);
}

// The regression. Before the fix, onExit() did not end the overlay, so leaving via
// Home or sleep left the book's look sitting on the live global fields.
TEST(ReaderEditOverlayLifetime, LeavingViaHomeOrSleepAlsoEndsTheOverlay) {
  Settings s;
  Reader reader(s);
  reader.openReaderSettings();
  ASSERT_EQ(s.live, kBookLook);

  // REPLACE: no result handler runs — the activity is simply destroyed.
  reader.onExit();

  EXPECT_FALSE(s.overlayActive) << "overlay outlived the reader";
  EXPECT_EQ(s.live, kGlobalLook) << "the book's look is stuck on the global reader fields";
  EXPECT_EQ(s.sinkCtx, nullptr) << "sink still points at the destroyed reader";
}

// Why it matters: the next book without an override of its own reads the live
// globals, so a leaked overlay silently changes a different book.
TEST(ReaderEditOverlayLifetime, TheNextBookDoesNotInheritTheLastBooksLook) {
  Settings s;
  {
    Reader first(s);
    first.openReaderSettings();
    first.onExit();  // left via Home
  }
  const Look inheritedByNextBook = s.live;
  EXPECT_EQ(inheritedByNextBook, kGlobalLook);
}

// A background save landing mid-edit must still write the globals, never the book's
// values (CrossPointSettings.cpp:672-684).
TEST(ReaderEditOverlayLifetime, ASaveMidEditPersistsGlobalsNotTheBook) {
  Settings s;
  Reader reader(s);
  reader.openReaderSettings();

  s.saveToFile();
  EXPECT_EQ(s.persisted, kGlobalLook) << "a book's per-book look leaked into settings.json";

  reader.onExit();
  s.saveToFile();
  EXPECT_EQ(s.persisted, kGlobalLook);
}

// The model above cannot notice if the real onExit() loses its end-of-overlay call,
// so read the production source and check it is still there. Same technique the
// grid/status screen audits use.
#include <fstream>
#include <sstream>
#include <string>

namespace {
std::string readSource(const char* path) {
  std::ifstream file(path);
  EXPECT_TRUE(file.is_open()) << "cannot open " << path;
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}
}  // namespace

TEST(ReaderEditOverlayLifetime, TheReaderStillEndsTheOverlayOnExit) {
  const std::string source = readSource(EPUB_READER_SOURCE);
  const auto onExit = source.find("void EpubReaderActivity::onExit()");
  ASSERT_NE(onExit, std::string::npos);
  // Bounded by the next function so a call somewhere else in the file cannot pass this.
  const auto nextFn = source.find("\nvoid EpubReaderActivity::", onExit + 1);
  const std::string body = source.substr(onExit, nextFn - onExit);
  EXPECT_NE(body.find("endReaderEditOverlay"), std::string::npos)
      << "onExit() no longer ends the reader-edit overlay: leaving via Home or sleep "
         "will strand the book's look on the global reader fields and leave the sink "
         "pointing at a destroyed activity.";
  EXPECT_NE(body.find("clearStatusBarOverride"), std::string::npos)
      << "onExit() no longer clears the status-bar override.";
}
