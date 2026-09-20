#pragma once

#include <utility>

// Destroy retained capacity as well as contents (including nested owners).
// Like the font downloader's empty-vector swap; allocates nothing.
template <typename T>
void releaseStorage(T& storage) {
  T empty;
  using std::swap;
  swap(storage, empty);
}
