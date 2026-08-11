// Host proof of the cold-boot reconcile decisions: the fingerprint is order-
// independent and change-sensitive, the membership set suppresses favorite-
// rename duplicates, and decidePlan picks the cheap path exactly when the
// folder is unchanged.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "sleep/SleepIndexReconcilePolicy.h"
#include "util/FavoriteImageNames.h"

using sleep_reconcile::DecideInput;
using sleep_reconcile::NameHashSet;
using sleep_reconcile::Plan;

namespace {

struct Entry {
  std::string name;
  uint32_t mtime;
  uint32_t size;
};

uint32_t fingerprintOf(const std::vector<Entry>& entries) {
  uint32_t sum = 0;
  for (const auto& e : entries) sum += sleep_reconcile::entryHash(e.name, e.mtime, e.size);
  return sum;
}

// The membership rule the device's pass C applies to each scanned name.
bool knownToIndex(const NameHashSet& set, const std::string& name) {
  if (set.contains(sleep_reconcile::nameHash(name))) return true;
  const std::string alt = FavoriteImage::favoriteCounterpart(name);
  return !alt.empty() && set.contains(sleep_reconcile::nameHash(alt));
}

DecideInput cleanInput() {
  DecideInput in;
  in.indexUsable = true;
  in.scannedLive = 100;
  in.scannedFingerprint = 42;
  in.snapLive = 100;
  in.snapFingerprint = 42;
  in.recordCount = 100;
  return in;
}

}  // namespace

// FAT directory order is arbitrary, so the fingerprint must not depend on it.
TEST(SleepIndexReconcile, FingerprintIsOrderIndependent) {
  std::vector<Entry> a = {{"x.pxc", 100, 5000}, {"y.bmp", 200, 6000}, {"z.pxc", 300, 7000}};
  std::vector<Entry> b = {a[2], a[0], a[1]};
  EXPECT_EQ(fingerprintOf(a), fingerprintOf(b));
}

// Any change — name, mtime, or size — moves the fingerprint. Size matters
// because macOS/Windows preserve source mtimes on copy, so a same-name
// replacement can carry the old mtime.
TEST(SleepIndexReconcile, FingerprintSeesEveryKindOfChange) {
  const std::vector<Entry> base = {{"x.pxc", 100, 5000}, {"y.bmp", 200, 6000}};
  const uint32_t fp = fingerprintOf(base);

  EXPECT_NE(fp, fingerprintOf({{"x.pxc", 100, 5000}}));                                       // removed
  EXPECT_NE(fp, fingerprintOf({{"x.pxc", 100, 5000}, {"y.bmp", 200, 6000}, {"n.pxc", 1, 2}}));  // added
  EXPECT_NE(fp, fingerprintOf({{"x2.pxc", 100, 5000}, {"y.bmp", 200, 6000}}));                // renamed
  EXPECT_NE(fp, fingerprintOf({{"x.pxc", 100, 5001}, {"y.bmp", 200, 6000}}));                 // replaced (size)
  EXPECT_NE(fp, fingerprintOf({{"x.pxc", 101, 5000}, {"y.bmp", 200, 6000}}));                 // touched (mtime)
}

// A favorite toggle renames the file; the counterpart membership check must
// refuse to append the renamed file as "new" — in both directions.
TEST(SleepIndexReconcile, CounterpartSuppressesFavoriteRenameAppends) {
  NameHashSet set;
  set.add(sleep_reconcile::nameHash("a.pxc"));
  set.add(sleep_reconcile::nameHash("b_F.bmp"));
  set.finalize();

  EXPECT_TRUE(knownToIndex(set, "a.pxc"));    // unchanged
  EXPECT_TRUE(knownToIndex(set, "a_F.pxc"));  // favorited since the index was built
  EXPECT_TRUE(knownToIndex(set, "b_F.bmp"));  // unchanged
  EXPECT_TRUE(knownToIndex(set, "b.bmp"));    // unfavorited since the index was built
  EXPECT_FALSE(knownToIndex(set, "c.pxc"));   // genuinely new
}

// A favorite toggle renames in place, and the device patches the persisted
// fingerprint by the two entry hashes rather than re-walking the folder. That
// patch has to land exactly where a full rescan would, or the saved snapshot
// starts lying and every later unlock scans again. mtime and size are the same
// on both sides because a FAT rename copies every field but the name.
TEST(SleepIndexReconcile, FavoriteRenamePatchMatchesAFullRescan) {
  const std::vector<Entry> before = {{"a.pxc", 111, 900}, {"b.pxc", 222, 1200}, {"c.bmp", 333, 1500}};
  const uint32_t snapshot = fingerprintOf(before);

  std::vector<Entry> after = before;
  after[1].name = "b_F.pxc";
  const uint32_t patched =
      snapshot + sleep_reconcile::entryHash("b_F.pxc", 222, 1200) - sleep_reconcile::entryHash("b.pxc", 222, 1200);
  EXPECT_EQ(patched, fingerprintOf(after));

  // Unfavoriting returns to the original, so a toggle on and off leaves no
  // drift for a later reconcile to trip over.
  const uint32_t undone =
      patched + sleep_reconcile::entryHash("b.pxc", 222, 1200) - sleep_reconcile::entryHash("b_F.pxc", 222, 1200);
  EXPECT_EQ(undone, snapshot);
}

// The point of the patch: with the snapshot re-anchored, the cold boot decides
// there is nothing to do. An unpatched snapshot would order an append pass that
// the counterpart rule then makes append nothing.
TEST(SleepIndexReconcile, PatchedSnapshotKeepsTheColdBootFromWalking) {
  auto in = cleanInput();
  in.scannedFingerprint = 1234;  // folder as it reads after the rename
  in.snapFingerprint = 1234;     // snapshot patched at rename time
  EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::NoChange);

  in.snapFingerprint = 999;  // the old behaviour: snapshot left stale
  EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::IncrementalAppend);
}

TEST(SleepIndexReconcile, NameHashSetMembership) {
  NameHashSet set;
  set.reserve(3);
  set.add(sleep_reconcile::nameHash("one.pxc"));
  set.add(sleep_reconcile::nameHash("two.pxc"));
  set.add(sleep_reconcile::nameHash("two.pxc"));  // duplicate records collapse
  set.finalize();
  EXPECT_EQ(set.size(), 2u);
  EXPECT_TRUE(set.contains(sleep_reconcile::nameHash("one.pxc")));
  EXPECT_FALSE(set.contains(sleep_reconcile::nameHash("three.pxc")));
}

// The decision matrix.
TEST(SleepIndexReconcile, DecidePlanMatrix) {
  // Unchanged folder, trusted index: the boot writes nothing.
  EXPECT_EQ(sleep_reconcile::decidePlan(cleanInput()), Plan::NoChange);

  // The dirty mark forces a re-check even when the fingerprint happens to match.
  {
    auto in = cleanInput();
    in.dirty = true;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::IncrementalAppend);
  }

  // A changed fingerprint is an incremental append.
  {
    auto in = cleanInput();
    in.scannedFingerprint = 43;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::IncrementalAppend);
  }

  // No usable index / wrong folder / stale flag: rebuild.
  {
    auto in = cleanInput();
    in.indexUsable = false;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::FullRebuild);
  }
  {
    auto in = cleanInput();
    in.dirChanged = true;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::FullRebuild);
  }
  {
    auto in = cleanInput();
    in.needsRebuildFlag = true;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::FullRebuild);
  }

  // Empty index + populated folder = first fill: rebuild so the first lap is
  // shuffled (an append would serve raw file order).
  {
    auto in = cleanInput();
    in.recordCount = 0;
    in.snapLive = 0;
    in.snapFingerprint = 0;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::FullRebuild);
  }

  // Over the incremental cap: rebuild rather than hash 20000 records.
  {
    auto in = cleanInput();
    in.recordCount = sleep_reconcile::kIncrementalMaxEntries + 1;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::FullRebuild);
  }

  // Dead-slot pileup: compact. 100 records, 20 live, no appends.
  {
    auto in = cleanInput();
    in.scannedLive = 20;
    in.recordCount = 100;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::FullRebuild);
  }

  // A few dead slots are fine (under max(64, count/4)).
  {
    auto in = cleanInput();
    in.scannedLive = 90;
    in.scannedFingerprint = 41;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::IncrementalAppend);
  }

  // Post-pass-C call: a huge append burst pushes past the hard cap → rebuild.
  {
    auto in = cleanInput();
    in.recordCount = 5000;
    in.pendingAppends = 16000;  // 5000 + 16000 > kMaxEntries
    in.scannedLive = 21000;
    EXPECT_EQ(sleep_reconcile::decidePlan(in), Plan::FullRebuild);
  }
}
