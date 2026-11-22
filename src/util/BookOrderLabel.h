#pragma once

#include <I18n.h>

#include "CrossPointSettings.h"

// The name of a browser order, as the light panel's Sort row and the file browser's
// header both print it.
inline StrId bookOrderLabel(const uint8_t order) {
  switch (order) {
    case CrossPointSettings::BOOK_ORDER_RANDOM:
      return StrId::STR_BOOK_ORDER_RANDOM;
    case CrossPointSettings::BOOK_ORDER_RECENTLY_ADDED:
      return StrId::STR_BOOK_ORDER_RECENTLY_ADDED;
    case CrossPointSettings::BOOK_ORDER_LAST_READ:
      return StrId::STR_BOOK_ORDER_LAST_READ;
    default:
      return StrId::STR_BOOK_ORDER_ALPHABETICAL;
  }
}
