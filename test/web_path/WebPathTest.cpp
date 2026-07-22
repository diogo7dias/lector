// Host tests for the WebPath safe-path module — the single brain both the web
// server and the WebDAV handler route path validation through. Pure string
// logic (no SD, no HTTP), so the interface is the whole test surface.
#include <gtest/gtest.h>

#include "WebPath.h"

// ── normalize ────────────────────────────────────────────────────────────────
// Canonical form: absolute (single leading '/'), no duplicate/trailing slashes,
// '..' resolved, empty/root collapse to "/".

TEST(WebPathNormalize, EmptyBecomesRoot) { EXPECT_EQ(WebPath::normalize(""), "/"); }

TEST(WebPathNormalize, RootStaysRoot) { EXPECT_EQ(WebPath::normalize("/"), "/"); }

TEST(WebPathNormalize, RepeatedSlashesCollapseToRoot) { EXPECT_EQ(WebPath::normalize("///"), "/"); }

TEST(WebPathNormalize, AbsolutePathUnchanged) { EXPECT_EQ(WebPath::normalize("/books/a.epub"), "/books/a.epub"); }

TEST(WebPathNormalize, RelativeGainsLeadingSlash) { EXPECT_EQ(WebPath::normalize("books/a.epub"), "/books/a.epub"); }

TEST(WebPathNormalize, DoubleSlashCollapses) { EXPECT_EQ(WebPath::normalize("/books//a.epub"), "/books/a.epub"); }

TEST(WebPathNormalize, TrailingSlashStripped) { EXPECT_EQ(WebPath::normalize("/books/sub/"), "/books/sub"); }

TEST(WebPathNormalize, DotDotResolves) { EXPECT_EQ(WebPath::normalize("/books/../a.epub"), "/a.epub"); }

TEST(WebPathNormalize, LeadingDotDotCannotEscapeRoot) { EXPECT_EQ(WebPath::normalize("/../../etc"), "/etc"); }

// ── isReservedName ───────────────────────────────────────────────────────────
// Single-name membership in the reserved system-name list. Case-sensitive, to
// match the existing exact-equals behaviour. Used by directory listers that keep
// their own dot-file visibility policy.

TEST(WebPathReserved, SystemVolumeInformationIsReserved) {
  EXPECT_TRUE(WebPath::isReservedName("System Volume Information"));
}

TEST(WebPathReserved, XtcacheIsReserved) { EXPECT_TRUE(WebPath::isReservedName("XTCache")); }

TEST(WebPathReserved, OrdinaryNameIsNotReserved) { EXPECT_FALSE(WebPath::isReservedName("book.epub")); }

TEST(WebPathReserved, ReservedMatchIsCaseSensitive) { EXPECT_FALSE(WebPath::isReservedName("xtcache")); }

TEST(WebPathReserved, DotNameIsNotReservedByNameAlone) {
  // A dot-prefixed name is hidden, but reserved-ness is only about the explicit
  // list; the dot rule lives in isProtected. Keeps the two concerns separable.
  EXPECT_FALSE(WebPath::isReservedName(".config"));
}

// ── isProtected ──────────────────────────────────────────────────────────────
// Per-segment guard: an operation is forbidden if ANY path segment is
// dot-prefixed or a reserved name. This is the strong rule adopted everywhere.

TEST(WebPathProtected, OrdinaryBookPathIsAllowed) { EXPECT_FALSE(WebPath::isProtected("/books/a.epub")); }

TEST(WebPathProtected, RootIsAllowed) { EXPECT_FALSE(WebPath::isProtected("/")); }

TEST(WebPathProtected, EmptyIsAllowed) { EXPECT_FALSE(WebPath::isProtected("")); }

TEST(WebPathProtected, BareOrdinaryNameIsAllowed) { EXPECT_FALSE(WebPath::isProtected("a.epub")); }

TEST(WebPathProtected, DotLeafIsProtected) { EXPECT_TRUE(WebPath::isProtected("/books/.secret.epub")); }

TEST(WebPathProtected, BareDotNameIsProtected) {
  // The rename handler validates a bare new name through the same rule.
  EXPECT_TRUE(WebPath::isProtected(".secret"));
}

TEST(WebPathProtected, HiddenIntermediateSegmentIsProtected) {
  // The strengthening the web server gains: a hidden FOLDER anywhere in the path
  // blocks the op, not just a hidden leaf.
  EXPECT_TRUE(WebPath::isProtected("/.hidden/a.epub"));
}

TEST(WebPathProtected, ReservedLeafIsProtected) { EXPECT_TRUE(WebPath::isProtected("/XTCache")); }

TEST(WebPathProtected, ReservedIntermediateSegmentIsProtected) {
  EXPECT_TRUE(WebPath::isProtected("/System Volume Information/book.epub"));
}
