// Source audit: the keys-only boards (X3, X4) keep the look they had before the
// FreeInkUI migration, and the touch board keeps the SDK's.
//
// UIThemeTokens.h cannot be compiled on the host (it pulls BoardConfig, the
// renderer and the HAL), so the split is checked by reading it — the same
// approach as test/device_look, which covers the touch-only *shapes* while this
// covers the *look*.
#include <gtest/gtest.h>

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

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

// The body of the keys-only branch in uiThemeTokens().
std::string keysOnlyBranch() {
  const std::string source = readSource(THEME_TOKENS_HEADER);
  const std::size_t start = source.find("if (!gpio.hasTouch())");
  EXPECT_NE(start, std::string::npos) << "the keys-only look is no longer applied under a hasTouch() guard";
  if (start == std::string::npos) return {};
  const std::size_t end = source.find("\n  }\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

// --- the split itself -------------------------------------------------------

TEST(KeysOnlyLook, TheLookIsSplitAtRuntimeNotAtCompileTime) {
  // X3 and X4 share one C3 binary with the X4 Pro's environment being separate,
  // but the keys-only pair cannot be told from a touch board at compile time —
  // only hasTouch() knows.
  const std::string source = readSource(THEME_TOKENS_HEADER);
  EXPECT_TRUE(contains(source, "gpio.hasTouch()"));
  EXPECT_FALSE(contains(source, "#ifdef FREEINK_DEVICE_X4PRO"))
      << "the look split must be a runtime one; a macro cannot separate X3/X4 from a touch board";
}

// --- regular, never bold, everywhere in the menus ---------------------------

TEST(KeysOnlyLook, NoMenuTextAsksForBold) {
  const std::string branch = keysOnlyBranch();
  EXPECT_TRUE(contains(branch, "tokens.titleText.bold = false"))
      << "the header title is still bold on the keys-only boards";
  EXPECT_TRUE(contains(branch, "tokens.bodyText.bold = false"));
  EXPECT_TRUE(contains(branch, "tokens.smallText.bold = false"));
  EXPECT_FALSE(contains(branch, "bold = true")) << "something in the keys-only look still asks for a bold face";
}

// --- headers: black band, white text ----------------------------------------

TEST(KeysOnlyLook, TheHeaderBandIsBlackWithWhiteText) {
  const std::string branch = keysOnlyBranch();
  EXPECT_TRUE(contains(branch, "tokens.popup.normal.background = fui::Paint::solid(fui::Color::Black)"))
      << "the header band is not filled black";
  EXPECT_TRUE(contains(branch, "tokens.popup.normal.foreground = fui::Paint::solid(fui::Color::White)"));
  EXPECT_TRUE(contains(branch, "tokens.titleText.color = fui::Color::White"))
      << "the header title is not knocked out of the band";
}

TEST(KeysOnlyLook, TheBlackHeaderCarriesNoUnderline) {
  // A rule under a filled band is the white-header look showing through.
  EXPECT_TRUE(contains(keysOnlyBranch(), "tokens.headerUnderline = 0"));
}

// --- selected rows: black band, white text ----------------------------------

TEST(KeysOnlyLook, TheSelectedRowIsFilledAndItsTextKnockedOut) {
  EXPECT_TRUE(contains(keysOnlyBranch(), "tokens.listSelectionStyle = fui::SelectionStyle::InvertFill"))
      << "the selected row is not the filled black band with white text";
}

// --- a setting's name and its value match -----------------------------------

TEST(KeysOnlyLook, TheSettingValueTakesTheSameStyleAsItsName) {
  const std::string source = readSource(THEME_TOKENS_HEADER);
  EXPECT_TRUE(contains(source, "props.valueText = tokens.bodyText"))
      << "the value is still themed from smallText, so it is a size away from the name beside it";
  EXPECT_TRUE(contains(source, "if (gpio.hasTouch()) return;"))
      << "the value restyle is not guarded; the touch UI would lose its deliberate hierarchy";
}

TEST(KeysOnlyLook, EveryListScreenGetsTheValueStyleWithoutAskingForIt) {
  // Applied in the shared viewport sync, so a new list screen cannot forget it.
  const std::string source = readSource(LIST_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(source, "applyKeysOnlyValueStyle(props, screen.theme())"));
  const std::size_t apply = source.find("applyKeysOnlyValueStyle");
  const std::size_t sync = source.find("void UiListActivity::syncListViewport");
  ASSERT_NE(apply, std::string::npos);
  ASSERT_NE(sync, std::string::npos);
  EXPECT_GT(apply, sync) << "the value style must be applied inside syncListViewport, which every list calls";
}

TEST(KeysOnlyLook, EveryListScreenGetsInvertedSectionHeadersWithoutAskingForIt) {
  const std::string lists = readSource(LIST_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(lists, "applyInvertedSectionHeaderStyle(props, screen.theme())"));
  const std::string status = readSource(STATUS_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(status, "applyInvertedSectionHeaderStyle(props, theme)"));
}

// --- the touch board is left alone ------------------------------------------

TEST(KeysOnlyLook, TheTouchBoardKeepsTheSdkLook) {
  // Everything above lives inside the !hasTouch() branch, so a touch board
  // reaches `return tokens` with the SDK's own values.
  const std::string source = readSource(THEME_TOKENS_HEADER);
  const std::size_t guard = source.find("if (!gpio.hasTouch())");
  ASSERT_NE(guard, std::string::npos);
  for (const char* keysOnly :
       {"titleText.bold = false", "headerUnderline = 0", "titleText.color = fui::Color::White"}) {
    EXPECT_GT(source.find(keysOnly), guard) << keysOnly << " is applied outside the keys-only guard";
  }
}

// Availability is decided before either settings view selects or renders rows.
TEST(KeysOnlyLook, FrontRemapIsOnlyOfferedOnX3AndX4) {
  const std::string source = readSource(SETTINGS_ACTIVITY_SOURCE);
  const std::string entry =
      "controlsSettings.push_back(SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, "
      "SettingAction::RemapFrontButtons));";
  const auto row = source.find(entry);
  ASSERT_NE(row, std::string::npos);
  EXPECT_EQ(source.find(entry, row + entry.size()), std::string::npos);
  EXPECT_TRUE(contains(source, ("if (gpio.isXteinkDevice()) {\n    " + entry + "\n  }").c_str()));
  EXPECT_LT(source.find("void SettingsActivity::rebuildSettingsList()"), row);
  EXPECT_LT(row, source.find("settings.insert(settings.end()"));

  const std::string gpioSource = readSource(GPIO_SOURCE);
  const auto start = gpioSource.find("bool HalGPIO::isXteinkDevice() const {");
  ASSERT_NE(start, std::string::npos);
  const auto end = gpioSource.find("\n}", start);
  ASSERT_NE(end, std::string::npos);
  const std::string predicate = gpioSource.substr(start, end - start);
  EXPECT_TRUE(contains(predicate,
                       "return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||\n"
                       "         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||\n"
                       "         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4;"));
  EXPECT_FALSE(contains(predicate, "XteinkX4Pro"));
}
