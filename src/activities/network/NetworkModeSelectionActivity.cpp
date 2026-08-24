#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int MENU_ITEM_COUNT = 4;
constexpr StrId MENU_ITEMS[MENU_ITEM_COUNT] = {StrId::STR_JOIN_NETWORK, StrId::STR_CALIBRE_WIRELESS,
                                               StrId::STR_CREATE_HOTSPOT, StrId::STR_NEARBY_TRANSFER};
constexpr StrId MENU_DESCS[MENU_ITEM_COUNT] = {StrId::STR_JOIN_DESC, StrId::STR_CALIBRE_DESC, StrId::STR_HOTSPOT_DESC,
                                               StrId::STR_NEARBY_TRANSFER_DESC};
constexpr UIIcon MENU_ICONS[MENU_ITEM_COUNT] = {UIIcon::Wifi, UIIcon::Library, UIIcon::Hotspot, UIIcon::Transfer};
constexpr NetworkMode MENU_MODES[MENU_ITEM_COUNT] = {NetworkMode::JOIN_NETWORK, NetworkMode::CONNECT_CALIBRE,
                                                     NetworkMode::CREATE_HOTSPOT, NetworkMode::NEARBY_READER};
}  // namespace

void NetworkModeSelectionActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

int NetworkModeSelectionActivity::listCount() const { return MENU_ITEM_COUNT; }

const char* NetworkModeSelectionActivity::headerTitle() const { return tr(STR_FILE_TRANSFER); }

void NetworkModeSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  rows.assign(MENU_ITEM_COUNT, fui::ListItem{});
  for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
    rows[i].label = I18N.get(MENU_ITEMS[i]);
    rows[i].subtitle = I18N.get(MENU_DESCS[i]);
    rows[i].icon = listIconFor(MENU_ICONS[i], 32);
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.iconSize = 32;
  syncListViewport(screen, props, true);
  screen.list(props);
}

void NetworkModeSelectionActivity::activateIndex(const int index) {
  app.clearTapFlash();
  onModeSelected(MENU_MODES[index]);
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
