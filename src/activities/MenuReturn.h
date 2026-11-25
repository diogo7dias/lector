#pragma once

#include <utility>

// Back from a screen a menu row opened returns to that menu.
//
// The reader menus and every OptionPopup close before the screen their row opens, so the
// activity stack alone lands Back on whatever is under the menu: the page, the file list.
// A row's action runs inside a MenuReturn::Scope instead. The first child screen started
// inside it (Activity::startActivityForResult) takes the reopen with it, and
// ActivityManager calls it when that child closes by Back, after the child's result
// handler has run. A child that closes any other way (a chapter picked, a Yes) lands
// where it always did.
struct MenuReturn {
  void (*reopen)(void* ctx, int row) = nullptr;
  void* ctx = nullptr;
  int row = -1;

  explicit operator bool() const { return reopen != nullptr; }
  void run() const {
    if (reopen) reopen(ctx, row);
  }

  // The reopen armed for the row action running right now, if any.
  static MenuReturn& armed() {
    static MenuReturn slot;
    return slot;
  }
  // What a child screen started now takes with it. Taking disarms: one row opens one screen.
  static MenuReturn take() { return std::exchange(armed(), MenuReturn{}); }

  // Held across one row's action. Disarms on the way out, so an action that opened no
  // screen (a toggle, a jump) leaves nothing armed for the next screen to pick up.
  class Scope {
   public:
    Scope(void (*reopen)(void* ctx, int row), void* ctx, const int row) { armed() = MenuReturn{reopen, ctx, row}; }
    ~Scope() { armed() = MenuReturn{}; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
  };
};
