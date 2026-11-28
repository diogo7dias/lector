// Source audit: cancelling Wi-Fi selection or failing ESP-NOW startup leaves no STA radio on.
//
// HalPowerManager::setPowerSaving() refuses to downclock while WiFi.getMode() != WIFI_MODE_NULL,
// so any screen that stays open after a cancelled or failed STA bring-up pins the CPU at full
// frequency and keeps the radio idling until the user leaves or the device sleeps.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

std::string bodyOf(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return {};
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

TEST(WifiRadioOffOnCancel, CancellingWifiSelectionTurnsStaOffBeforeModeSelection) {
  const std::string source = readFile(std::filesystem::path(NETWORK_ACTIVITY_DIR) / "CrossPointWebServerActivity.cpp");
  const std::string fn = bodyOf(source, "void CrossPointWebServerActivity::onWifiSelectionComplete(");
  ASSERT_FALSE(fn.empty()) << "onWifiSelectionComplete() not found";

  const size_t cancelBranch = fn.find("} else {");
  ASSERT_NE(cancelBranch, std::string::npos) << "cancel branch not found";
  const size_t disconnect = fn.find("WiFi.disconnect(true);", cancelBranch);
  const size_t modeOff = fn.find("WiFi.mode(WIFI_OFF);", cancelBranch);
  const size_t relaunch = fn.find("NetworkModeSelectionActivity", cancelBranch);

  ASSERT_NE(disconnect, std::string::npos) << "cancel branch must call WiFi.disconnect(true)";
  ASSERT_NE(modeOff, std::string::npos) << "cancel branch must call WiFi.mode(WIFI_OFF)";
  ASSERT_NE(relaunch, std::string::npos);
  EXPECT_LT(disconnect, modeOff);
  EXPECT_LT(modeOff, relaunch);
}

TEST(WifiRadioOffOnCancel, FailedNearbyTransportBeginTurnsStaOffBeforeReturning) {
  const std::string source = readFile(std::filesystem::path(NETWORK_ACTIVITY_DIR) / "NearbyFileTransferActivity.cpp");
  const std::string fn = bodyOf(source, "void NearbyFileTransferActivity::onEnter()");
  ASSERT_FALSE(fn.empty()) << "NearbyFileTransferActivity::onEnter() not found";

  const size_t beginFail = fn.find("if (!transport.begin(");
  ASSERT_NE(beginFail, std::string::npos) << "transport.begin() failure guard not found";
  const size_t failEnd = fn.find("return;", beginFail);
  ASSERT_NE(failEnd, std::string::npos);
  const std::string block = fn.substr(beginFail, failEnd - beginFail);

  const size_t disconnect = block.find("WiFi.disconnect(true);");
  const size_t modeOff = block.find("WiFi.mode(WIFI_OFF);");
  ASSERT_NE(disconnect, std::string::npos) << "transport.begin() failure must call WiFi.disconnect(true)";
  ASSERT_NE(modeOff, std::string::npos) << "transport.begin() failure must call WiFi.mode(WIFI_OFF)";
  EXPECT_LT(disconnect, modeOff);
}
