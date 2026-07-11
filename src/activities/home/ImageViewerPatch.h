#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace image_viewer_patch {

enum class Action { None, Erase, Rename };

struct Plan {
  Action action = Action::None;
  std::string sourceName;
  std::string finalName;
  bool valid = true;
};

// sourcePath is empty when the viewer only navigated or returned unchanged.
// When a file changed, it identifies the row before that move/delete/rename.
Plan plan(const std::string& finalPath, const std::string& sourcePath);

// Returns the selector row for a source index before it is erased. Keeping that
// row selected makes the entry that shifts into the old slot the next choice.
std::optional<size_t> selectorForSource(size_t sourceIndex, size_t headerRows,
                                        const std::vector<size_t>* filteredIndexes);

}  // namespace image_viewer_patch
