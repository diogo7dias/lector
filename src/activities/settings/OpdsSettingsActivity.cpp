#include "OpdsSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "OpdsTokenStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Editable fields: Name, URL, Username, Password, then the custom headers.
// Existing servers also show a Delete option (BASE_ITEMS + 1).
constexpr int FIELD_ITEMS = 4;
constexpr int HEADER_ITEMS = static_cast<int>(OpdsServer::MAX_CUSTOM_HEADERS);
constexpr int BASE_ITEMS = FIELD_ITEMS + HEADER_ITEMS;

constexpr StrId FIELD_NAMES[BASE_ITEMS] = {StrId::STR_SERVER_NAME,     StrId::STR_OPDS_SERVER_URL,
                                           StrId::STR_USERNAME,        StrId::STR_PASSWORD,
                                           StrId::STR_CUSTOM_HEADER_1, StrId::STR_CUSTOM_HEADER_2};

// One keyboard line holds "Name: value", so it has to fit the longest of both.
constexpr uint16_t HEADER_LINE_MAX = HTTP_HEADER_NAME_MAX + 2 + HTTP_HEADER_VALUE_MAX;
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

ListChrome OpdsSettingsActivity::chrome() const {
  ListChrome chrome;
  // Reuse STR_OPDS_BROWSER as the "edit existing server" title.
  // New server creation uses STR_ADD_SERVER.
  chrome.title = isNewServer ? tr(STR_ADD_SERVER) : tr(STR_OPDS_BROWSER);
  // What the middle button does here is not obvious from the rows, so it gets
  // its own band under the title.
  chrome.subHeader = tr(STR_CALIBRE_URL_HINT);
  return chrome;
}

bool OpdsSettingsActivity::drawOverlay() {
  if (!showSaveError) return false;
  GUI.drawPopup(renderer, errorMessage);
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
  errorMessage = tr(STR_ERROR_GENERAL_FAILURE);
  if (showSaveError) {
    requestUpdate();
  }

  return success;
}

void OpdsSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  showSaveError = false;

  // Each field edit is saved immediately so partially configured servers
  // survive navigation and power-loss scenarios.
  if (index >= FIELD_ITEMS && index < BASE_ITEMS) {
    // "Name: value" on one line, so a header takes one keyboard visit and one row. The line
    // opens as it was saved: the value is already readable to anyone who can edit this row.
    const size_t slot = static_cast<size_t>(index - FIELD_ITEMS);
    auto handler = [this, slot](const ActivityResult& result) {
      if (result.isCancelled) return;
      HttpHeader header;
      parseHttpHeaderLine(std::get<KeyboardResult>(result.data).text, header);
      if (!isValidHttpHeader(header.name, header.value)) {
        errorMessage = tr(STR_INVALID_HEADER);
        showSaveError = true;
        requestUpdate();
        return;
      }
      editServer.customHeaders[slot] = std::move(header);
      saveServer();
      requestUpdate();
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(FIELD_NAMES[index]),
                                                                   formatHttpHeaderLine(editServer.customHeaders[slot]),
                                                                   HEADER_LINE_MAX, InputType::Text),
                           handler);
    return;
  }

  if (index >= 0 && index < FIELD_ITEMS) {
    // One handler for all four fields: they differ only in where the text lands, what
    // the keyboard is titled, how long it may be, and which keyboard it opens.
    struct FieldSpec {
      std::string OpdsServer::* target;
      StrId title;
      uint16_t maxLength;
      InputType type;
    };
    static constexpr FieldSpec fields[FIELD_ITEMS] = {
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
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(field.title),
                                                                   prefill, field.maxLength, field.type),
                           handler);
    return;
  }

  if (index == BASE_ITEMS && !isNewServer) {
    // Delete flow is only available for existing servers. Confirmed first, like every
    // other delete: the row sits right under the fields, one press too far down.
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_SERVER), editServer.name),
        [this](const ActivityResult& result) {
          if (result.isCancelled) {
            requestUpdate();
            return;
          }
          // The sign-in token goes with the server it belongs to.
          OPDS_TOKENS.put(OpdsTokenStore::keyFor(editServer.url, editServer.username), {});
          if (!OPDS_STORE.removeServer(static_cast<size_t>(serverIndex))) {
            LOG_ERR("OPS", "Failed to remove OPDS server at index %d", serverIndex);
            showSaveError = true;
            requestUpdate();
            return;
          }
          finish();
        });
  }
}

void OpdsSettingsActivity::buildScreen(UiScreen& screen) {
  const int itemCount = getMenuItemCount();
  subtitles.assign(static_cast<size_t>(itemCount), std::string());
  rows.assign(static_cast<size_t>(itemCount), fui::ListItem{});

  for (int i = 0; i < itemCount; ++i) {
    if (i >= FIELD_ITEMS && i < BASE_ITEMS) {
      // A header's name shows, its value never does: the row says only that it has one.
      rows[i].label = I18N.get(FIELD_NAMES[i]);
      const HttpHeader& header = editServer.customHeaders[static_cast<size_t>(i - FIELD_ITEMS)];
      subtitles[i] = header.name.empty() ? tr(STR_NOT_SET) : formatHttpHeaderLine(header, /*maskValue=*/true);
      rows[i].subtitle = subtitles[i].c_str();
    } else if (i < BASE_ITEMS) {
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
