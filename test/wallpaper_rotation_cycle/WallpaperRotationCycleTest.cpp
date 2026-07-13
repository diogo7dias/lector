// Rotation-cycle regression for the sleep wallpaper playlist.
//
// The reported bug: the lock-screen wallpaper ping-ponged between two images
// instead of cycling through all of them. Root cause was two engines (the RAM
// buffer engine and the low-heap direct walk) tracking SEPARATE cursors over a
// MUTATED (move-to-back) order, so the wake-to-wake heap-gate engine flip
// desynced them and the anti-repeat guard bounced between two files.
//
// The fix makes rotation a single shared cursor (lastShownSleepFilename) walking
// a STABLE order (the order file is written only by reconcile/reshuffle, never by
// a pick). This drives the REAL WallpaperPlaylistV2::advance() — the buffer
// engine — across simulated deep-sleep reboots and asserts it cycles every file
// once, wraps, skips vanished files, shows new uploads next, and keeps a
// favorited file's slot. The engine-FLIP itself lives in the hardware-bound
// Wallpaper.cpp nextSleepFile and is device-verified; the direct walk there
// (orderNextAfter) computes the identical shared-cursor successor proven here.

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "sleep/SleepFs.h"
#include "sleep/WallpaperPlaylistV2.h"
#include "sleep/persist/IFileIO.h"

using crosspoint::sleep::ISleepFs;
using crosspoint::sleep::SleepBmpEntry;
using crosspoint::sleep::v2::WallpaperPlaylistV2;

namespace {

// Favorite iff the basename carries "_F" before its extension. counterpart()
// toggles it (x.pxc <-> x_F.pxc), matching the production FavoriteImage helpers.
std::string favCounterpart(const std::string& name) {
  const auto dot = name.find_last_of('.');
  const std::string stem = (dot == std::string::npos) ? name : name.substr(0, dot);
  const std::string ext = (dot == std::string::npos) ? "" : name.substr(dot);
  if (stem.size() >= 2 && stem.compare(stem.size() - 2, 2, "_F") == 0) {
    return stem.substr(0, stem.size() - 2) + ext;
  }
  return stem + "_F" + ext;
}

// In-memory order-file store that survives a simulated reboot (the map lives in
// the Harness, not the reset singleton).
class FakeFileIO : public crosspoint::persist::IFileIO {
 public:
  std::unordered_map<std::string, std::string> files;

  bool safeWrite(const std::string& path, const std::string& content) override {
    files[path] = content;
    return true;
  }
  bool safeWriteStreamed(const std::string& path, const crosspoint::persist::StreamProducer& produce) override {
    struct StringSink : crosspoint::persist::JsonSink {
      std::string out;
      size_t write(uint8_t b) override {
        out.push_back(static_cast<char>(b));
        return 1;
      }
      size_t write(const uint8_t* buf, size_t n) override {
        out.append(reinterpret_cast<const char*>(buf), n);
        return n;
      }
    } sink;
    if (!produce(sink)) return false;
    files[path] = sink.out;
    return true;
  }
  std::string safeRead(const std::string& path) override {
    auto it = files.find(path);
    return it == files.end() ? std::string() : it->second;
  }
  bool exists(const std::string& path) override { return files.count(path) > 0; }
  bool mkdir(const std::string&) override { return true; }
  bool copy(const std::string&, const std::string&) override { return true; }
};

// In-memory /sleep folder. Order in `files` is the directory listing order.
class FakeSleepFs : public ISleepFs {
 public:
  struct Entry {
    std::string name;
    uint32_t mtime;
  };
  std::vector<Entry> files;

  bool has(const std::string& name) const {
    return std::any_of(files.begin(), files.end(), [&](const Entry& e) { return e.name == name; });
  }
  void remove(const std::string& name) {
    files.erase(std::remove_if(files.begin(), files.end(), [&](const Entry& e) { return e.name == name; }),
                files.end());
  }

  size_t countSleepBmps(size_t cap) override { return std::min(files.size(), cap); }
  std::vector<std::string> listSleepBmps(size_t maxEntries) override {
    std::vector<std::string> o;
    for (const auto& e : files) {
      if (o.size() >= maxEntries) break;
      o.push_back(e.name);
    }
    return o;
  }
  std::string nextSleepBmpAfter(const std::string&) override { return {}; }
  std::string nthSleepBmp(size_t) override { return {}; }
  bool exists(const std::string& path) override {
    const std::string pfx = "/sleep/";
    if (path.rfind(pfx, 0) != 0) return false;
    return has(path.substr(pfx.size()));
  }
  bool mkdir(const std::string&) override { return true; }
  bool rename(const std::string&, const std::string&) override { return true; }
  std::vector<SleepBmpEntry> listSleepBmpsWithMtime(size_t maxEntries) override {
    std::vector<SleepBmpEntry> o;
    for (const auto& e : files) {
      if (o.size() >= maxEntries) break;
      o.push_back({e.name, e.mtime});
    }
    return o;
  }
  void walkSleepBmps(const std::function<void(const char*, size_t, uint32_t)>& cb) override {
    for (const auto& e : files) cb(e.name.c_str(), e.name.size(), e.mtime);
  }
};

// One rotation surface. wake() simulates a deep-sleep reboot: reset the singleton
// (drops its RAM buffer), re-wire the SAME persisted order file + shared cursor,
// and take one pick. largestFree lets a test flip the heap regime.
struct Harness {
  FakeFileIO io;
  FakeSleepFs fs;
  std::string lastShown;
  std::string lastRendered;

  std::string wake(size_t largestFree = static_cast<size_t>(1) << 20) {
    auto& pl = WallpaperPlaylistV2::instance();
    pl.resetForTest();
    WallpaperPlaylistV2::Deps d;
    d.fs = &fs;
    d.fileIO = &io;
    d.orderFilePath = "/.crosspoint/sleep_order.txt";
    d.lastShownFilename = &lastShown;
    d.lastRenderedPath = &lastRendered;
    d.largestFreeBlockFn = [largestFree]() { return largestFree; };
    d.favoriteCounterpartFn = [](const std::string& n) { return favCounterpart(n); };
    pl.setDeps(d);
    return pl.advance();
  }
};

Harness sixFiles() {
  Harness h;
  // mtime DESC (newest first) fixes the order to A..F.
  h.fs.files = {{"A.pxc", 106}, {"B.pxc", 105}, {"C.pxc", 104}, {"D.pxc", 103}, {"E.pxc", 102}, {"F.pxc", 101}};
  return h;
}

}  // namespace

// The core regression: cycle every file once, in order, then wrap — no ping-pong.
TEST(WallpaperRotationCycle, VisitsEveryFileOnceThenWraps) {
  Harness h = sixFiles();
  std::vector<std::string> seq;
  for (int i = 0; i < 7; ++i) seq.push_back(h.wake());

  const std::vector<std::string> expected = {"A.pxc", "B.pxc", "C.pxc", "D.pxc", "E.pxc", "F.pxc", "A.pxc"};
  EXPECT_EQ(seq, expected);

  // Explicit no-ping-pong / no-clustering: the first six picks are all distinct.
  std::set<std::string> firstLap(seq.begin(), seq.begin() + 6);
  EXPECT_EQ(firstLap.size(), 6u);
}

// Keeps cycling over many reboots (13 wakes = two full laps + wrap).
TEST(WallpaperRotationCycle, KeepsCyclingAcrossManyReboots) {
  Harness h = sixFiles();
  std::vector<std::string> seq;
  for (int i = 0; i < 13; ++i) seq.push_back(h.wake());
  const std::vector<std::string> expected = {"A.pxc", "B.pxc", "C.pxc", "D.pxc", "E.pxc", "F.pxc", "A.pxc",
                                             "B.pxc", "C.pxc", "D.pxc", "E.pxc", "F.pxc", "A.pxc"};
  EXPECT_EQ(seq, expected);
}

// The successor walk is independent of the heap regime once the order exists:
// flipping largestFree wake-to-wake still cycles all six (models the engine
// flip being harmless now that both engines share one cursor + order).
TEST(WallpaperRotationCycle, StableUnderAlternatingHeapRegime) {
  Harness h = sixFiles();
  h.wake();  // build the order file (front A)
  std::vector<std::string> seq;
  for (int i = 0; i < 6; ++i) {
    const size_t heap = (i % 2 == 0) ? (static_cast<size_t>(1) << 20) : (static_cast<size_t>(2) << 20);
    seq.push_back(h.wake(heap));
  }
  EXPECT_EQ(seq, (std::vector<std::string>{"B.pxc", "C.pxc", "D.pxc", "E.pxc", "F.pxc", "A.pxc"}));
}

// A vanished file (moved to /sleep pause, deleted) is skipped, not stuck on.
TEST(WallpaperRotationCycle, SkipsVanishedFile) {
  Harness h = sixFiles();
  EXPECT_EQ(h.wake(), "A.pxc");
  EXPECT_EQ(h.wake(), "B.pxc");
  EXPECT_EQ(h.wake(), "C.pxc");  // cursor now C
  h.fs.remove("D.pxc");          // D disappears from the folder
  EXPECT_EQ(h.wake(), "E.pxc");  // successor of C skips the vanished D
}

// A freshly-uploaded file is shown on the very next pick (new-on-top).
TEST(WallpaperRotationCycle, NewUploadShownNext) {
  Harness h = sixFiles();
  EXPECT_EQ(h.wake(), "A.pxc");
  EXPECT_EQ(h.wake(), "B.pxc");
  h.fs.files.insert(h.fs.files.begin(), {"G.pxc", 999});  // newest upload
  EXPECT_EQ(h.wake(), "G.pxc");
}

// Favoriting the just-shown file (rename x -> x_F) keeps its rotation slot: it is
// not re-shown, and rotation advances to the successor.
TEST(WallpaperRotationCycle, FavoriteRenameKeepsSlot) {
  Harness h = sixFiles();
  EXPECT_EQ(h.wake(), "A.pxc");
  EXPECT_EQ(h.wake(), "B.pxc");  // cursor B
  // User favorites B: the file is renamed and the shared cursor follows the
  // rename (production FavoriteImage::updateSleepReferencesOnPathChange).
  for (auto& e : h.fs.files) {
    if (e.name == "B.pxc") e.name = "B_F.pxc";
  }
  h.lastShown = "B_F.pxc";
  EXPECT_EQ(h.wake(), "C.pxc");  // not B_F again — successor
}
