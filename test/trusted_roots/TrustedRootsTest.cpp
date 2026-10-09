#include <gtest/gtest.h>

#include "TrustedRoots.h"

using trusted_roots::forUrl;

TEST(TrustedRoots, TheFirmwareUpdateHostsAreVerified) {
  EXPECT_EQ(forUrl("https://api.github.com/repos/diogo7dias/lector/releases/latest"), trusted_roots::GITHUB);
  EXPECT_EQ(forUrl("https://github.com/diogo7dias/lector/releases/download/x/firmware.bin"), trusted_roots::GITHUB);
  EXPECT_EQ(forUrl("https://release-assets.githubusercontent.com/a?b=c"), trusted_roots::GITHUB);
  EXPECT_EQ(forUrl("HTTPS://API.GITHUB.COM:443/x"), trusted_roots::GITHUB);
}

TEST(TrustedRoots, TheDefaultSyncServerIsVerified) {
  EXPECT_EQ(forUrl("https://sync.crosspointreader.com/users/auth"), trusted_roots::CROSSPOINT_SYNC);
}

TEST(TrustedRoots, LookalikeAndOtherHostsStayUnverified) {
  EXPECT_EQ(forUrl("https://evilgithub.com/x"), nullptr);
  EXPECT_EQ(forUrl("https://github.com.evil.net/x"), nullptr);
  EXPECT_EQ(forUrl("https://my-calibre.local:8080/opds"), nullptr);
  EXPECT_EQ(forUrl("http://api.github.com/x"), nullptr);  // plain http has no TLS to verify
}
