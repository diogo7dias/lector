#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace LibrarySearchSupport {

std::string searchLabelForEntry(const std::string& entry);

std::vector<size_t> rankMatches(const std::vector<std::string>& entries, const std::string& query);

// Streaming variant: ranks `count` entries pulled one at a time via `nameAt`, so the
// caller need not hold every name in RAM (used by the SD-backed large-folder index).
std::vector<size_t> rankMatches(size_t count, const std::function<std::string(size_t)>& nameAt,
                                const std::string& query);

}  // namespace LibrarySearchSupport
