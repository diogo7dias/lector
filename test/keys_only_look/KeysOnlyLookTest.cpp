// Source audit: one flat look on every board, touch or keys. Regular weight, one
// size, a black header band with white text, the selected row the same filled band,
// no scroll track, names and values in the same style. None of it may be gated on
// hasTouch(): the X4 Pro was once left with the SDK's own look that way.
//
// UIThemeTokens.h cannot be compiled on the host (it pulls BoardConfig and the
// renderer), so the look is checked by reading it, the same approach as
// test/device_look.
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

// The body of uiThemeTokens().
std::string tokensBody() {
  const std::string source = readSource(THEME_TOKENS_HEADER);
  const std::size_t start = source.find("inline freeink::ui::ThemeTokens uiThemeTokens(");
  EXPECT_NE(start, std::string::npos);
  if (start == std::string::npos) return {};
  const std::size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

// --- one look, no board split -----------------------------------------------

TEST(FlatLook, NothingInTheThemeTokensAsksWhetherTheBoardHasTouch) {
  const std::string source = readSource(THEME_TOKENS_HEADER);
  EXPECT_FALSE(contains(source, "hasTouch()"))
      << "the look is split on the board again; the X4 Pro must get the same one";
  EXPECT_FALSE(contains(source, "#ifdef FREEINK_DEVICE_X4PRO"));
}

// --- regular, never bold, everywhere in the menus ---------------------------

TEST(FlatLook, NoMenuTextAsksForBold) {
  const std::string body = tokensBody();
  EXPECT_TRUE(contains(body, "tokens.titleText.bold = false")) << "the header title still asks for bold";
  EXPECT_TRUE(contains(body, "tokens.bodyText.bold = false"));
  EXPECT_TRUE(contains(body, "tokens.smallText.bold = false"));
  EXPECT_FALSE(contains(body, "bold = true")) << "something in the look still asks for a bold face";
}

// --- headers: black band, white text ----------------------------------------

TEST(FlatLook, TheHeaderBandIsBlackWithWhiteText) {
  const std::string body = tokensBody();
  EXPECT_TRUE(contains(body, "tokens.popup.normal.background = fui::Paint::solid(fui::Color::Black)"))
      << "the header band is not filled black";
  EXPECT_TRUE(contains(body, "tokens.popup.normal.foreground = fui::Paint::solid(fui::Color::White)"));
  EXPECT_TRUE(contains(body, "tokens.titleText.color = fui::Color::White"))
      << "the header title is not knocked out of the band";
  EXPECT_TRUE(contains(body, "tokens.headerUnderline = 0")) << "a rule under a filled band is the white-header look";
}

// --- selected rows: black band, white text ----------------------------------

TEST(FlatLook, TheSelectedRowIsFilledAndItsTextKnockedOut) {
  EXPECT_TRUE(contains(tokensBody(), "tokens.listSelectionStyle = fui::SelectionStyle::InvertFill"))
      << "the selected row is not the filled black band with white text";
}

// --- no scroll track: chevrons say "more" -----------------------------------

TEST(FlatLook, TheSdkDrawsNoScrollTrack) {
  EXPECT_TRUE(contains(tokensBody(), "tokens.listScrollWidth = 0"))
      << "the SDK still draws a track; lists indicate overflow with the chevrons instead";
  EXPECT_TRUE(contains(readSource(LIST_ACTIVITY_SOURCE), "drawScrollArrows"));
  EXPECT_TRUE(contains(readSource(STATUS_ACTIVITY_SOURCE), "drawScrollArrows"));
}

// --- a setting's name and its value match -----------------------------------

TEST(FlatLook, TheSettingValueTakesTheSameStyleAsItsName) {
  const std::string source = readSource(THEME_TOKENS_HEADER);
  EXPECT_TRUE(contains(source, "props.valueText = tokens.bodyText"))
      << "the value is still themed from smallText, so it is a size away from the name beside it";
}

TEST(FlatLook, EveryListScreenGetsTheSharedRowStylesWithoutAskingForIt) {
  // Applied in the shared viewport sync, so a new list screen cannot forget them.
  const std::string source = readSource(LIST_ACTIVITY_SOURCE);
  const std::size_t sync = source.find("void UiListActivity::syncListViewport");
  ASSERT_NE(sync, std::string::npos);
  for (const char* call :
       {"applyKeysOnlyValueStyle(props, screen.theme())", "applyWrappingRowStyle(props, screen.theme())",
        "applyInvertedSectionHeaderStyle(props, screen.theme())"}) {
    const std::size_t apply = source.find(call);
    EXPECT_NE(apply, std::string::npos) << call;
    EXPECT_GT(apply, sync) << call << " must be applied inside syncListViewport, which every list calls";
  }
  const std::string status = readSource(STATUS_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(status, "applyWrappingRowStyle(props, theme)"));
  EXPECT_TRUE(contains(status, "applyInvertedSectionHeaderStyle(props, theme)"));
}

// --- labels wrap, never truncate --------------------------------------------

TEST(FlatLook, ListLabelsMayWrap) {
  const std::string source = readSource(THEME_TOKENS_HEADER);
  EXPECT_TRUE(contains(source, "props.labelText.maxLines = kListLabelMaxLines"));
  EXPECT_TRUE(contains(source, "kListLabelMaxLines = 4"));
}

// Availability is decided before either settings view selects or renders rows.
TEST(FlatLook, FrontRemapIsOnlyOfferedOnX3AndX4) {
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
