#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

struct WifiResult {
  bool connected = false;
  std::string ssid;
  std::string ip;
};

struct KeyboardResult {
  std::string text;
};

struct MenuResult {
  int action = -1;
  uint8_t orientation = 0;
  uint8_t paragraphNumbering = 0;
  // Per-book paragraph-number size (0 = Small, 1 = Double), cycled in the menu.
  uint8_t paragraphNumberSize = 1;
  // Per-book Paperback Look, toggled live in the menu and applied by the reader on close.
  uint8_t paperbackBody = 1;
  uint8_t paperbackStatus = 1;
  // Per-book status bar master switch, toggled live in the menu and applied by the
  // reader on close. Unlike the Paperback flags this one repaginates: the bar's
  // reserved bands change the viewport.
  uint8_t statusBar = 1;
  // The global Progress Bar value (CrossPointSettings::STATUS_BAR_OFF_BAR), cycled in the
  // menu on the row that only exists while this book's status bar is off. Global, not
  // per-book, exactly like Book Bar / Chapter Bar / Bar Thickness. Repaginates too.
  uint8_t progressBar = 0;
  // The bound Menu Hold function (a CrossPointSettings::LONG_PRESS_MENU_FUNCTION
  // value) when the menu was closed by holding Confirm, else LP_MENU_DISABLED. The
  // reader runs it; the menu only reports the hold. Defaults to LP_MENU_DISABLED (1),
  // NOT 0 — 0 is LP_MENU_KOSYNC, so a zero default would sync on every menu close.
  uint8_t holdFunction = 1;
};

struct ChapterResult {
  int spineIndex = 0;
  std::string anchor;
};

struct PercentResult {
  int percent = 0;
};

struct IntervalResult {
  uint32_t value = 0;
};

struct PageResult {
  uint32_t page = 0;
};

struct ProgressChangeResult {
  int spineIndex = 0;
  int page = 0;
  int totalPages = 0;
  std::string xpath;
  float percentage = 0.0f;
  bool hasSavedProgress = false;
  // Exact visible-codepoint offset within spineIndex, when the source (a bookmark) has one.
  // Preferred over xpath/percentage on resolution: it is immune to re-pagination.
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 0;
};

enum class NetworkMode;

struct NetworkModeResult {
  NetworkMode mode;
};

struct FootnoteResult {
  std::string href;
};

struct FilePathResult {
  std::string path;
};

// Index into ReaderPresetStore of the Reading Theme the user chose to apply. Only the
// index travels: the reader reads the theme back from the store, so the two cannot
// disagree if the screen edited it on the way out.
struct PresetResult {
  size_t index = 0;
};

using ResultVariant =
    std::variant<std::monostate, WifiResult, KeyboardResult, MenuResult, ChapterResult, PercentResult, IntervalResult,
                 PageResult, ProgressChangeResult, NetworkModeResult, FootnoteResult, FilePathResult, PresetResult>;

struct ActivityResult {
  bool isCancelled = false;
  ResultVariant data;

  explicit ActivityResult() = default;

  template <typename ResultType>
    requires std::is_constructible_v<ResultVariant, ResultType&&>
  // cppcheck-suppress noExplicitConstructor
  ActivityResult(ResultType&& result) : data{std::forward<ResultType>(result)} {}
};

using ActivityResultHandler = std::function<void(const ActivityResult&)>;
