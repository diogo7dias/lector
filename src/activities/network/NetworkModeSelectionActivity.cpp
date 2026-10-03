#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
struct MenuEntry {
  StrId label;
  StrId description;
  UIIcon icon;
  NetworkMode mode;
};
constexpr MenuEntry MENU[] = {
    {StrId::STR_JOIN_NETWORK, StrId::STR_JOIN_DESC, UIIcon::Wifi, NetworkMode::JOIN_NETWORK},
    {StrId::STR_CALIBRE_WIRELESS, StrId::STR_CALIBRE_DESC, UIIcon::Library, NetworkMode::CONNECT_CALIBRE},
    {StrId::STR_CREATE_HOTSPOT, StrId::STR_HOTSPOT_DESC, UIIcon::Hotspot, NetworkMode::CREATE_HOTSPOT},
    {StrId::STR_NEARBY_TRANSFER, StrId::STR_NEARBY_TRANSFER_DESC, UIIcon::Transfer, NetworkMode::NEARBY_READER},
#if FREEINK_CAP_USB_MSC
    {StrId::STR_USB_DRIVE, StrId::STR_USB_DRIVE_DESC, UIIcon::Usb, NetworkMode::USB_DRIVE},
#endif
};
constexpr int MENU_ITEM_COUNT = sizeof(MENU) / sizeof(MENU[0]);
}  // namespace

void NetworkModeSelectionActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

int NetworkModeSelectionActivity::listCount() const { return MENU_ITEM_COUNT; }

const char* NetworkModeSelectionActivity::headerTitle() const { return tr(STR_FILE_TRANSFER); }

void NetworkModeSelectionActivity::buildScreen(UiScreen& screen) {
  rows.assign(MENU_ITEM_COUNT, fui::ListItem{});
  for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
    rows[i].label = I18N.get(MENU[i].label);
    rows[i].subtitle = I18N.get(MENU[i].description);
    rows[i].icon = listIconFor(MENU[i].icon, 32);
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
  onModeSelected(MENU[index].mode);
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
