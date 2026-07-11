#include "ImageViewerPatch.h"

namespace {
bool extractName(const std::string& path, std::string& name) {
  const auto pos = path.find_last_of('/');
  name = pos == std::string::npos ? path : path.substr(pos + 1);
  return !name.empty();
}
}  // namespace

image_viewer_patch::Plan image_viewer_patch::plan(const std::string& finalPath, const std::string& sourcePath) {
  Plan result;
  if (sourcePath.empty()) return result;
  if (!extractName(sourcePath, result.sourceName)) {
    result.valid = false;
    return result;
  }
  if (finalPath.empty()) {
    result.action = Action::Erase;
    return result;
  }
  if (!extractName(finalPath, result.finalName)) {
    result.valid = false;
    return result;
  }
  if (result.finalName != result.sourceName) result.action = Action::Rename;
  return result;
}

std::optional<size_t> image_viewer_patch::selectorForSource(const size_t sourceIndex, const size_t headerRows,
                                                            const std::vector<size_t>* const filteredIndexes) {
  if (filteredIndexes == nullptr) return headerRows + sourceIndex;
  for (size_t i = 0; i < filteredIndexes->size(); ++i) {
    if ((*filteredIndexes)[i] == sourceIndex) return headerRows + i;
  }
  return std::nullopt;
}
