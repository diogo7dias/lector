#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#ifndef MAPPED_INPUT_MANAGER_SOURCE
#error "MAPPED_INPUT_MANAGER_SOURCE must be defined by the build system"
#endif

namespace {

std::string source() {
  std::ifstream file(MAPPED_INPUT_MANAGER_SOURCE);
  EXPECT_TRUE(file.is_open()) << "cannot open " << MAPPED_INPUT_MANAGER_SOURCE;
  std::stringstream body;
  body << file.rdbuf();
  return body.str();
}

}  // namespace

TEST(HintBandInput, ReleaseFirstQueryArmsTheSyntheticStroke) {
  const std::string body = source();
  const std::size_t arm = body.find("hintPendingRelease = static_cast<int>(hw);");
  const std::size_t release = body.find("if (releaseQuery) return false;", arm);
  ASSERT_NE(arm, std::string::npos);
  ASSERT_NE(release, std::string::npos);
  EXPECT_LT(arm, release) << "A release-only caller must arm the next-frame synthetic release before the tap frame returns false.";
}
