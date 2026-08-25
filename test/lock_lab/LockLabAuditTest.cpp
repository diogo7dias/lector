// Source audit: the Lock Lab is a bench, not a feature. Every line of it must sit behind
// LECTOR_LOCK_LAB so it cannot reach a release build.
//
// The risk this guards is not a compile error, which would be loud. It is the opposite: a
// lab row added outside the guard builds fine, ships, and puts a developer bench on a
// reader's home screen. The guard is also the argument that keeps the lab acceptable at
// all under the project's "not a Swiss Army knife" rule, so it has to be enforced rather
// than remembered.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef LOCK_LAB_SOURCES
#error "LOCK_LAB_SOURCES must be defined by the build system"
#endif

namespace {

constexpr const char* kGuard = "LECTOR_LOCK_LAB";

std::vector<std::string> sourcePaths() {
  std::vector<std::string> paths;
  std::stringstream all(LOCK_LAB_SOURCES);
  std::string path;
  while (std::getline(all, path, '|')) {
    if (!path.empty()) paths.push_back(path);
  }
  return paths;
}

std::vector<std::string> readLines(const std::string& path) {
  std::vector<std::string> lines;
  std::ifstream file(path);
  EXPECT_TRUE(file.is_open()) << "cannot open " << path;
  std::string line;
  while (std::getline(file, line)) lines.push_back(line);
  return lines;
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

// True only for a real preprocessor directive: '#' as the first non-blank character.
// Matching "#if" anywhere in the line would let a comment that merely mentions #ifdef
// open a conditional that never closes, after which every following line would count as
// guarded and the audit would pass no matter what was added.
bool isDirective(const std::string& line, const char* keyword) {
  const size_t at = line.find_first_not_of(" \t");
  if (at == std::string::npos || line[at] != '#') return false;
  return line.compare(at, std::string(keyword).size(), keyword) == 0;
}

// Tracks #if nesting so a line can be asked whether some enclosing conditional is the
// lab guard. Deliberately simple: it understands #ifdef/#ifndef/#if/#else/#endif and
// nothing else, which is all this codebase's preprocessor use amounts to.
class GuardTracker {
 public:
  void feed(const std::string& line) {
    if (isDirective(line, "#if")) {
      stack.push_back(contains(line, kGuard));
      return;
    }
    if (stack.empty()) return;
    // #else flips the sense of the innermost conditional: code after `#else` on an
    // `#ifdef LECTOR_LOCK_LAB` is the NOT-lab branch and must not hold lab code.
    if (isDirective(line, "#else") || isDirective(line, "#elif")) {
      stack.back() = false;
      return;
    }
    if (isDirective(line, "#endif")) stack.pop_back();
  }

  bool guarded() const {
    for (const bool isGuard : stack) {
      if (isGuard) return true;
    }
    return false;
  }

 private:
  std::vector<bool> stack;
};

}  // namespace

TEST(LockLabAudit, EveryMentionSitsBehindTheGuard) {
  const auto paths = sourcePaths();
  ASSERT_FALSE(paths.empty());

  std::vector<std::string> offenders;
  for (const std::string& path : paths) {
    const auto lines = readLines(path);
    ASSERT_FALSE(lines.empty()) << path;
    GuardTracker tracker;
    for (size_t i = 0; i < lines.size(); ++i) {
      const std::string& line = lines[i];
      const bool guardedBefore = tracker.guarded();
      tracker.feed(line);
      // The directive that opens or closes the guard names it without being inside it.
      if (isDirective(line, "#")) continue;
      if (!contains(line, "locklab") && !contains(line, "LockLab") && !contains(line, "LOCK_LAB") &&
          !contains(line, "Lock Lab")) {
        continue;
      }
      // Comments explaining why something exists are allowed to name it in the clear.
      const size_t firstNonSpace = line.find_first_not_of(" \t");
      if (firstNonSpace != std::string::npos && line.compare(firstNonSpace, 2, "//") == 0) continue;
      if (guardedBefore || tracker.guarded()) continue;
      offenders.push_back(path + ":" + std::to_string(i + 1) + ": " + line);
    }
  }

  EXPECT_TRUE(offenders.empty()) << "Lock Lab code outside #ifdef " << kGuard << ":\n"
                                 << [&offenders] {
                                      std::string all;
                                      for (const std::string& offender : offenders) all += offender + "\n";
                                      return all;
                                    }();
}

TEST(LockLabAudit, TheLabItselfIsGuardedWholesale) {
  // The lab's own files open with the guard and close with it, so nothing in them can be
  // compiled by accident even if someone adds a source to the build.
  for (const std::string& path : sourcePaths()) {
    if (path.find("/dev/") == std::string::npos) continue;
    const auto lines = readLines(path);
    ASSERT_FALSE(lines.empty()) << path;
    bool opens = false;
    for (size_t i = 0; i < lines.size() && i < 6; ++i) {
      if (isDirective(lines[i], "#ifdef") && contains(lines[i], kGuard)) opens = true;
    }
    EXPECT_TRUE(opens) << path << " does not open with #ifdef " << kGuard;
  }
}
