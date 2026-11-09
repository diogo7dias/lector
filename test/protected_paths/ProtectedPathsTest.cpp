#include <gtest/gtest.h>

#include "network/ProtectedPaths.h"

using protected_paths::isProtectedFile;
using protected_paths::isProtectedName;
using protected_paths::isProtectedPath;

TEST(ProtectedPaths, StoresAndTheirSiblingsAreProtected) {
  EXPECT_TRUE(isProtectedFile("/.crosspoint/wifi.json"));
  EXPECT_TRUE(isProtectedFile("/.crosspoint/opds.json"));
  EXPECT_TRUE(isProtectedFile("/.crosspoint/koreader.json"));
  EXPECT_TRUE(isProtectedFile("/.crosspoint/trusted_certs.txt"));
  EXPECT_TRUE(isProtectedFile("/.crosspoint/wifi.json.tmp"));
  EXPECT_TRUE(isProtectedFile("/.CrossPoint/WIFI.json"));
}

TEST(ProtectedPaths, SpellingsThatOpenTheSameFileAreProtected) {
  EXPECT_TRUE(isProtectedFile("//.crosspoint//wifi.json"));
  EXPECT_TRUE(isProtectedFile("/.crosspoint/plugins/../wifi.json"));
  EXPECT_TRUE(isProtectedFile("/.crosspoint/wifi.json."));
  EXPECT_TRUE(isProtectedFile("/.crosspoint./wifi.json"));
  EXPECT_TRUE(isProtectedFile("/ .crosspoint/ wifi.json"));
}

TEST(ProtectedPaths, AliasesThatReachTheStoresAreProtected) {
  EXPECT_TRUE(isProtectedFile("/CROSSP~1/wifi.json"));
  EXPECT_TRUE(isProtectedFile("/CROSSP~1"));
  EXPECT_TRUE(isProtectedFile("/.crosspoint/WIFI~1.JSO"));
}

TEST(ProtectedPaths, AliasShapedNamesElsewhereStayReachable) {
  EXPECT_FALSE(isProtectedFile("/Books/IMG~1.JPG"));
  EXPECT_FALSE(isProtectedFile("/Books/Photos/IMG~1.JPG"));
  EXPECT_FALSE(isProtectedFile("/Books/a~b.epub"));
}

TEST(ProtectedPaths, OtherFilesAreNotProtectedFiles) {
  EXPECT_FALSE(isProtectedFile("/.crosspoint/settings.json"));
  EXPECT_FALSE(isProtectedFile("/Books/wifi.json"));
}

TEST(ProtectedPaths, ARequestIsRefusedWhenAnySegmentIsProtected) {
  EXPECT_TRUE(isProtectedPath("/.crosspoint/settings.json"));
  EXPECT_TRUE(isProtectedPath("/ .crosspoint/settings.json"));
  EXPECT_TRUE(isProtectedPath("/System Volume Information/x"));
  EXPECT_TRUE(isProtectedPath("/System Volume Information./x"));
  EXPECT_FALSE(isProtectedPath("/"));
  EXPECT_FALSE(isProtectedPath("/Books/a.epub"));
}

TEST(ProtectedPaths, ListedNamesAreNotReadAsAliases) {
  EXPECT_TRUE(isProtectedName(".crosspoint"));
  EXPECT_TRUE(isProtectedName("XTCache"));
  EXPECT_FALSE(isProtectedName("IMG~1.JPG"));
  EXPECT_FALSE(isProtectedName("a.epub"));
}
