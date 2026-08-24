#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int MENU_ITEMS = 8;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_USERNAME,          StrId::STR_PASSWORD,      StrId::STR_SYNC_SERVER_URL,
                                     StrId::STR_DOCUMENT_MATCHING, StrId::STR_SEND_METADATA, StrId::STR_SYNC_BEHAVIOR,
                                     StrId::STR_SIGN_UP,           StrId::STR_AUTHENTICATE};
}  // namespace

void KOReaderSettingsActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  values.clear();
}

int KOReaderSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* KOReaderSettingsActivity::headerTitle() const { return tr(STR_KOREADER_SYNC); }

void KOReaderSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  const size_t selectedIndex = static_cast<size_t>(index);
  if (selectedIndex == 0) {
    // Username
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                   KOREADER_STORE.getUsername(), 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               KOREADER_STORE.setCredentials(kb.text, KOREADER_STORE.getPassword());
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 1) {
    // Password
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                KOREADER_STORE.getPassword(), 64, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), kb.text);
            KOREADER_STORE.saveToFile();
          }
        });
  } else if (selectedIndex == 2) {
    // Sync Server URL - prefill with https:// if empty to save typing
    const std::string currentUrl = KOREADER_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SYNC_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               KOREADER_STORE.setServerUrl(urlToSave);
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 3) {
    // Document Matching - toggle between Filename and Binary
    const auto current = KOREADER_STORE.getMatchMethod();
    const auto newMethod =
        (current == DocumentMatchMethod::FILENAME) ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
    KOREADER_STORE.setMatchMethod(newMethod);
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 4) {
    // Send Metadata - toggle on/off
    KOREADER_STORE.setSendMetadata(!KOREADER_STORE.getSendMetadata());
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 5) {
    // Sync behavior - toggle between Ask and Smart
    const auto current = KOREADER_STORE.getSyncBehavior();
    const auto newBehavior = (current == KOReaderSyncBehavior::ASK_EVERY_TIME) ? KOReaderSyncBehavior::SMART
                                                                               : KOReaderSyncBehavior::ASK_EVERY_TIME;
    KOREADER_STORE.setSyncBehavior(newBehavior);
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 6) {
    // Sign Up - create a new account on the sync server with the entered credentials
    if (!KOREADER_STORE.hasCredentials()) {
      return;
    }
    startActivityForResult(
        std::make_unique<KOReaderAuthActivity>(renderer, mappedInput, KOReaderAuthActivity::Mode::SIGN_UP),
        [](const ActivityResult&) {});
  } else if (selectedIndex == 7) {
    // Authenticate
    if (!KOREADER_STORE.hasCredentials()) {
      // Can't authenticate without credentials - just show message briefly
      return;
    }
    startActivityForResult(std::make_unique<KOReaderAuthActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

std::string KOReaderSettingsActivity::statusFor(const int index) const {
  if (index == 0) {
    auto username = KOREADER_STORE.getUsername();
    return username.empty() ? std::string(tr(STR_NOT_SET)) : username;
  }
  if (index == 1) {
    return KOREADER_STORE.getPassword().empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
  }
  if (index == 2) {
    auto serverUrl = KOREADER_STORE.getServerUrl();
    if (!serverUrl.empty()) return serverUrl;
    // Show which server the default actually is, scheme stripped for space
    std::string defaultUrl = KOREADER_STORE.getBaseUrl();
    const auto schemeEnd = defaultUrl.find("://");
    if (schemeEnd != std::string::npos) defaultUrl.erase(0, schemeEnd + 3);
    return std::string(tr(STR_DEFAULT_VALUE)) + ": " + defaultUrl;
  }
  if (index == 3) {
    return KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? std::string(tr(STR_FILENAME))
                                                                            : std::string(tr(STR_BINARY));
  }
  if (index == 4) {
    return KOREADER_STORE.getSendMetadata() ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
  }
  if (index == 5) {
    return KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART ? std::string(tr(STR_SMART_SYNC))
                                                                          : std::string(tr(STR_ASK_EVERY_TIME));
  }
  if (index == 6 || index == 7) {
    // Both need credentials; saying so on the row beats a press that does nothing.
    return KOREADER_STORE.hasCredentials() ? std::string() : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
  }
  return std::string(tr(STR_NOT_SET));
}

void KOReaderSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  values.assign(MENU_ITEMS, std::string());
  rows.assign(MENU_ITEMS, fui::ListItem{});
  for (int i = 0; i < MENU_ITEMS; ++i) {
    values[i] = statusFor(i);
    rows[i].label = I18N.get(menuNames[i]);
    if (!values[i].empty()) rows[i].value = values[i].c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}
