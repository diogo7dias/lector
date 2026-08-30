#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "OpdsSettingsActivity.h"
#include "activities/ActivityManager.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/OpdsFilename.h"

namespace fui = freeink::ui;

namespace {
// Normalizes a user-typed folder: trims spaces, "" => SD root, otherwise a
// single leading '/' and no trailing '/'. Cold path (runs once per edit).
std::string normalizeFolder(std::string v) {
  while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
  if (v.empty()) return "";
  if (v.front() != '/') v.insert(v.begin(), '/');
  while (v.size() > 1 && v.back() == '/') v.pop_back();
  if (v == "/") return "";  // a bare slash is SD root, same as empty
  return v;
}

// Label shown for the current OPDS filename format in the list subtitle.
StrId opdsFormatLabel(uint8_t format) {
  switch (format) {
    case static_cast<uint8_t>(OpdsFilenameFormat::TitleAuthor):
      return StrId::STR_FMT_TITLE_AUTHOR;
    case static_cast<uint8_t>(OpdsFilenameFormat::TitleOnly):
      return StrId::STR_FMT_TITLE;
    default:
      return StrId::STR_FMT_AUTHOR_TITLE;
  }
}
}  // namespace

int OpdsServerListActivity::getItemCount() const {
  int count = static_cast<int>(OPDS_STORE.getCount());
  // Settings mode appends three virtual items: "Add Server", "Download folder"
  // and "Filename format".
  if (!pickerMode) {
    count += 3;
  }
  return count;
}

void OpdsServerListActivity::onEnter() {
  // Reload from disk in case servers were added/removed by a subactivity or the web UI
  OPDS_STORE.loadFromFile();
  UiListActivity::onEnter();
}

void OpdsServerListActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  labels.clear();
  subtitles.clear();
}

const char* OpdsServerListActivity::headerTitle() const { return tr(STR_OPDS_SERVERS); }

void OpdsServerListActivity::onBackButton() {
  // The picker is entered from the home menu, so Back belongs to that menu
  // rather than to whatever sits under this screen on the stack.
  if (pickerMode) {
    activityManager.goHome(HomeMenuItem::OPDS_BROWSER);
    return;
  }
  finish();
}

void OpdsServerListActivity::activateIndex(const int index) {
  app.clearTapFlash();
  const int selectedIndex = index;
  const auto serverCount = static_cast<int>(OPDS_STORE.getCount());

  if (pickerMode) {
    // Picker mode: selecting a server navigates to the OPDS browser
    if (selectedIndex < serverCount) {
      const auto* server = OPDS_STORE.getServer(static_cast<size_t>(selectedIndex));
      if (server) {
        activityManager.replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, *server));
      }
    }
    return;
  }

  // Index layout: [servers 0..serverCount-1], [Add Server], [Download folder], [Filename format].
  if (selectedIndex == serverCount + 1) {
    auto folderHandler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        const std::string norm = normalizeFolder(kb.text);
        strncpy(SETTINGS.opdsDownloadFolder, norm.c_str(), sizeof(SETTINGS.opdsDownloadFolder) - 1);
        SETTINGS.opdsDownloadFolder[sizeof(SETTINGS.opdsDownloadFolder) - 1] = '\0';
        SETTINGS.saveToFile();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_DOWNLOAD_FOLDER),
                                                std::string(SETTINGS.opdsDownloadFolder), 63, InputType::Text),
        folderHandler);
    return;
  }

  // "Filename format": tap cycles through the available formats.
  if (selectedIndex == serverCount + 2) {
    SETTINGS.opdsFilenameFormat =
        static_cast<uint8_t>((SETTINGS.opdsFilenameFormat + 1) % static_cast<uint8_t>(OpdsFilenameFormat::Count));
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  // Settings mode: open editor for selected server, or create a new one
  auto resultHandler = [this](const ActivityResult&) {
    // Reload server list when returning from editor
    OPDS_STORE.loadFromFile();
    nav.reset();
    requestUpdate();
  };

  if (selectedIndex < serverCount) {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, selectedIndex), resultHandler);
  } else {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

void OpdsServerListActivity::buildScreen(UiScreen& screen) {
  const int itemCount = getItemCount();
  if (itemCount == 0) {
    screen.centeredText(tr(STR_NO_SERVERS));
    return;
  }

  const auto& servers = OPDS_STORE.getServers();
  const auto serverCount = static_cast<int>(servers.size());

  labels.assign(static_cast<size_t>(itemCount), std::string());
  subtitles.assign(static_cast<size_t>(itemCount), std::string());
  rows.assign(static_cast<size_t>(itemCount), fui::ListItem{});

  for (int i = 0; i < itemCount; ++i) {
    // Primary label: server name (falling back to URL if unnamed).
    // Secondary label: server URL (shown as subtitle when the name is set).
    if (i < serverCount) {
      labels[i] = servers[i].name.empty() ? servers[i].url : servers[i].name;
      if (!servers[i].name.empty()) subtitles[i] = servers[i].url;
    } else if (i == serverCount) {
      labels[i] = I18N.get(StrId::STR_ADD_SERVER);
    } else if (i == serverCount + 1) {
      labels[i] = I18N.get(StrId::STR_OPDS_DOWNLOAD_FOLDER);
      const char* folder = SETTINGS.opdsDownloadFolder;
      subtitles[i] = folder[0] ? std::string(folder) : std::string(I18N.get(StrId::STR_OPDS_SD_ROOT));
    } else {
      labels[i] = I18N.get(StrId::STR_OPDS_FILENAME_FORMAT);
      subtitles[i] = I18N.get(opdsFormatLabel(SETTINGS.opdsFilenameFormat));
    }
    rows[i].label = labels[i].c_str();
    if (!subtitles[i].empty()) rows[i].subtitle = subtitles[i].c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(itemCount);
  props.action = ACTION_ROW;
  syncListViewport(screen, props, true);
  screen.list(props);
}
