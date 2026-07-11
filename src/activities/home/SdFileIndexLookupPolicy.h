#pragma once

namespace sd_file_index_lookup {

enum class Mode { Binary, Linear };

constexpr Mode mode(const bool shuffled, const bool renamedInPlace) {
  return shuffled || renamedInPlace ? Mode::Linear : Mode::Binary;
}

}  // namespace sd_file_index_lookup
