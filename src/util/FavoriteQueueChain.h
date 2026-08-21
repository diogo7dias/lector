#pragma once

#include <string>
#include <vector>

// Pure resolution of a queued-rename chain (no Arduino, no FreeRTOS — host testable).
//
// DeferredFavorite holds renames in a queue and runs them later. Until it drains, the card
// still holds the OLD name, so anything that draws a file name, or decides which way the
// next toggle goes, has to ask what the file WILL be called.
//
// The answer is a chain, not a lookup. A file can be renamed more than once before the
// queue drains — favorite it, favorite something else, unfavorite it — and the queue then
// holds A to B and later B back to A with an unrelated job between them. Reading only the
// newest job whose source is A answers B, when the name the file actually ends up with is
// A. That wrong answer is not merely cosmetic: the next toggle derives its direction from
// it and enqueues a rename the worker cannot perform.
namespace favorite_chain {

struct Job {
  std::string fromPath;
  std::string toPath;
};

// The name `fromPath` ends up with once every queued job has run, or an empty string when
// the queue leaves it where it is. Jobs must be in queue order, oldest first.
inline std::string resolve(const std::vector<Job>& jobs, const std::string& fromPath) {
  std::string current = fromPath;
  for (const Job& job : jobs) {
    if (job.fromPath == current) current = job.toPath;
  }
  return current == fromPath ? std::string() : current;
}

}  // namespace favorite_chain
