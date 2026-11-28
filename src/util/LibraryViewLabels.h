#pragma once

#include <I18n.h>
#include <LibraryFormat.h>

#include <cstdio>

namespace library_view {

inline StrId sortLabel(const library::LibrarySort sort) {
  switch (sort) {
    case library::LibrarySort::Author:
      return StrId::STR_SORT_AUTHOR;
    case library::LibrarySort::RecentlyAdded:
      return StrId::STR_BOOK_ORDER_RECENTLY_ADDED;
    case library::LibrarySort::LastRead:
      return StrId::STR_BOOK_ORDER_LAST_READ;
    case library::LibrarySort::Title:
    default:
      return StrId::STR_SORT_TITLE;
  }
}

inline StrId filterLabel(const library::LibraryFilter filter) {
  switch (filter) {
    case library::LibraryFilter::Reading:
      return StrId::STR_FILTER_READING;
    case library::LibraryFilter::Unread:
      return StrId::STR_FILTER_UNREAD;
    case library::LibraryFilter::Read:
      return StrId::STR_FILTER_READ;
    case library::LibraryFilter::All:
    default:
      return StrId::STR_FILTER_ALL;
  }
}

// Formats the subheader when a non-default sort or filter is active; returns false for default (Title + All).
inline bool formatSubHeader(const library::LibrarySort sort, const library::LibraryFilter filter, char* out,
                            const size_t size) {
  if (sort == library::LibrarySort::Title && filter == library::LibraryFilter::All) {
    if (size > 0) out[0] = '\0';
    return false;
  }
  if (filter != library::LibraryFilter::All && sort != library::LibrarySort::Title) {
    snprintf(out, size, "%s · %s", I18N.get(filterLabel(filter)), I18N.get(sortLabel(sort)));
  } else if (filter != library::LibraryFilter::All) {
    snprintf(out, size, "%s: %s", I18N.get(StrId::STR_FILTER), I18N.get(filterLabel(filter)));
  } else {
    snprintf(out, size, "%s: %s", I18N.get(StrId::STR_SORT), I18N.get(sortLabel(sort)));
  }
  return true;
}

}  // namespace library_view
