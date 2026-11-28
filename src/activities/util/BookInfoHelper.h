#pragma once

#include <Epub.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <functional>
#include <string>
#include <utility>

#include "activities/Activity.h"
#include "activities/RenderLock.h"
#include "activities/reader/DictionaryDefinitionActivity.h"
#include "components/BusyBanner.h"

// Opens the paged Book Info view (cover, title, author, publisher description) for an EPUB.
// Shared by FileBrowserActivity and LibraryActivity.
inline void openBookInfoScreen(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::string& fullPath, std::function<void()> onDone) {
  std::string title;
  std::string text;
  std::string cover;
  {
    RenderLock lock(host);
    BusyBanner banner(renderer, tr(STR_LOADING));
    auto epub = makeUniqueNoThrow<Epub>(fullPath, "/.crosspoint");
    if (!epub) {
      LOG_ERR("BOOKINFO", "OOM: Epub");
      if (onDone) onDone();
      return;
    }
    epub->setupCacheDir();
    if (!epub->load(true, true)) {
      LOG_ERR("BOOKINFO", "Book info: cannot load %s", fullPath.c_str());
      if (onDone) onDone();
      return;
    }
    title = epub->getTitle();
    const std::string& author = epub->getAuthor();
    const std::string& description = epub->getDescription();
    if (!author.empty()) text = author + "\n\n";
    text += description.empty() ? tr(STR_NO_DESCRIPTION) : description;
    if (epub->generateCoverBmp(false)) cover = epub->getCoverBmpPath(false);
  }
  host.startActivityForResult(
      std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(title), std::move(text), false,
                                                     false, std::move(cover)),
      [onDone = std::move(onDone)](const ActivityResult&) {
        if (onDone) onDone();
      });
}
