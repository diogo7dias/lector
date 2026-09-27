#include <Fnv1a.h>
#include <gtest/gtest.h>

// Published FNV-1a vectors. These hashes name cache files and key persisted indexes, so a
// drift here silently orphans every cache on every card.
TEST(Fnv1a, Hash32MatchesReferenceVectors) {
  EXPECT_EQ(fnv1a::hash32(""), 0x811c9dc5u);
  EXPECT_EQ(fnv1a::hash32("a"), 0xe40c292cu);
  EXPECT_EQ(fnv1a::hash32("foobar"), 0xbf9cf968u);
}

TEST(Fnv1a, Hash64MatchesReferenceVectors) {
  EXPECT_EQ(fnv1a::hash64(""), 0xcbf29ce484222325ull);
  EXPECT_EQ(fnv1a::hash64("a"), 0xaf63dc4c8601ec8cull);
  EXPECT_EQ(fnv1a::hash64("foobar"), 0x85944171f73967e8ull);
}

TEST(Fnv1a, ContinuationEqualsOneShot) {
  const uint8_t bytes[] = {'f', 'o', 'o', 'b', 'a', 'r'};
  EXPECT_EQ(fnv1a::hash32(bytes + 3, 3, fnv1a::hash32(bytes, 3)), fnv1a::hash32("foobar"));
  EXPECT_EQ(fnv1a::hash64("bar", fnv1a::hash64("foo")), fnv1a::hash64("foobar"));
  EXPECT_EQ(fnv1a::mix32(fnv1a::hash32("fooba"), 'r'), fnv1a::hash32("foobar"));
  // Bytes above 0x7F must hash as unsigned, the same through the char and byte paths.
  const uint8_t high[] = {0xC3, 0xA9};
  EXPECT_EQ(fnv1a::hash32(high, 2), fnv1a::hash32("\xC3\xA9"));
}
