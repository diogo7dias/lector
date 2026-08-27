#include "OpdsSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Editable fields: Name, URL, Username, Password.
// Existing servers also show a Delete option (BASE_ITEMS + 1).
constexpr int BASE_ITEMS = 4;

constexpr StrId FIELD_NAMES[BASE_ITEMS] = {StrId::STR_SERVER_NAME, StrId::STR_OPDS_SERVER_URL, StrId::STR_USERNAME,
                                           StrId::STR_PASSWORD};
}  // namespace

int OpdsSettingsActivity::getMenuItemCount() const {
  return isNewServer ? BASE_ITEMS : BASE_ITEMS + 1;  // +1 for Delete
}

void OpdsSettingsActivity::onEnter() {
  isNewServer = (serverIndex < 0);
  showSaveError = false;

  if (!isNewServer) {
    // Edit flow: copy the selected server into local editable state.
    // Changes are persisted field-by-field through saveServer().
    const auto* server = OPDS_STORE.getServer(static_cast<size_t>(serverIndex));
    if (server) {
      editServer = *server;
    } else {
      // Server was deleted between navigation and entering this screen — treat as new
      isNewServer = true;
      serverIndex = -1;
    }
  }

  // Last: it resets the selection and asks for the first paint, so the state above
  // must already be settled.
  UiListActivity::onEnter();
}

void OpdsSettingsActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  subtitles.clear();
}

const char* OpdsSettingsActivity::headerTitle() const {
  // Reuse STR_OPDS_BROWSER as the "edit existing server" title.
  // New server creation uses STR_ADD_SERVER.
  return isNewServer ? tr(STR_ADD_SERVER) : tr(STR_OPDS_BROWSER);
}

void OpdsSettingsActivity::drawChrome() {
  UiListActivity::drawChrome();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawSubHeader(renderer,
                    Rect{0, metrics.topPadding + metrics.headerHeight, renderer.getScreenWidth(), metrics.tabBarHeight},
                    tr(STR_CALIBRE_URL_HINT));
}

bool OpdsSettingsActivity::drawOverlay() {
  if (!showSaveError) return false;
  GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  return true;
}

bool OpdsSettingsActivity::saveServer() {
  bool success = false;

  if (isNewServer) {
    // Create flow: first save inserts a new server record into the multi-server store.
    success = OPDS_STORE.addServer(editServer);
    if (success) {
      // After the first successful save, promote to an existing server so
      // subsequent field edits update in-place rather than creating duplicates.
      isNewServer = false;
      serverIndex = static_cast<int>(OPDS_STORE.getCount()) - 1;
    } else {
      LOG_ERR("OPS", "Failed to add OPDS server");
    }
  } else {
    // Edit flow: update the same server entry in-place.
    success = OPDS_STORE.updateServer(static_cast<size_t>(serverIndex), editServer);
    if (!success) {
      LOG_ERR("OPS", "Failed to update OPDS server at index %d", serverIndex);
    }
  }

  showSaveError = !success;
  if (showSaveError) {
    requestUpdate();
  }

  return success;
}

void OpdsSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();

  // Each field edit is saved immediately so partially configured servers
  // survive navigation and power-loss scenarios.
  if (index >= 0 && index < BASE_ITEMS) {
    // One handler for all four fields: they differ only in where the text lands, what
    // the keyboard is titled, how long it may be, and which keyboard it opens.
    struct FieldSpec {
      std::string OpdsServer::*target;
      StrId title;
      uint16_t maxLength;
      InputType type;
    };
    static constexpr FieldSpec fields[BASE_ITEMS] = {
        {&OpdsServer::name, StrId::STR_SERVER_NAME, 63, InputType::Text},
        {&OpdsServer::url, StrId::STR_OPDS_SERVER_URL, 127, InputType::Url},
        {&OpdsServer::username, StrId::STR_USERNAME, 63, InputType::Text},
        {&OpdsServer::password, StrId::STR_PASSWORD, 63, InputType::Password},
    };
    const FieldSpec& field = fields[index];

    // The URL field opens on a scheme rather than on nothing, and a user who leaves
    // just the scheme behind meant "empty", not "https://".
    const bool isUrl = field.target == &OpdsServer::url;
    std::string prefill = editServer.*(field.target);
    if (isUrl && prefill.empty()) prefill = "https://";

    auto handler = [this, target = field.target, isUrl](const ActivityResult& result) {
      if (result.isCancelled) return;
      const auto& kb = std::get<KeyboardResult>(result.data);
      editServer.*target = (isUrl && (kb.text == "https://" || kb.text == "http://")) ? std::string() : kb.text;
      saveServer();
      requestUpdate();
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(field.title), prefill,
                                                                   field.maxLength, field.type),
                           handler);
    return;
  }

  if (index == BASE_ITEMS && !isNewServer) {
    // Delete flow is only available for existing servers.
    if (!OPDS_STORE.removeServer(static_cast<size_t>(serverIndex))) {
      LOG_ERR("OPS", "Failed to remove OPDS server at index %d", serverIndex);
      showSaveError = true;
      requestUpdate();
      return;
    }
    finish();
  }
}

void OpdsSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The hint line drawChrome() paints sits between the header and the rows, so the top
  // margin carries it too.
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing),
      0, static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  const int itemCount = getMenuItemCount();
  subtitles.assign(static_cast<size_t>(itemCount), std::string());
  rows.assign(static_cast<size_t>(itemCount), fui::ListItem{});

  for (int i = 0; i < itemCount; ++i) {
    if (i < BASE_ITEMS) {
      rows[i].label = I18N.get(FIELD_NAMES[i]);
      // The value as the subtitle, which is where the old drawList value column put it.
      // A password is never shown, set or not: the row says only that it has one.
      const std::string* value = nullptr;
      switch (i) {
        case 0:
          value = &editServer.name;
          break;
        case 1:
          value = &editServer.url;
          break;
        case 2:
          value = &editServer.username;
          break;
        default:
          value = &editServer.password;
          break;
      }
      if (value->empty()) {
        subtitles[i] = tr(STR_NOT_SET);
      } else {
        subtitles[i] = (i == 3) ? std::string("******") : *value;
      }
      rows[i].subtitle = subtitles[i].c_str();
    } else {
      rows[i].label = tr(STR_DELETE_SERVER);
    }
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(itemCount);
  props.action = ACTION_ROW;
  syncListViewport(screen, props, true);
  screen.list(props);
}
