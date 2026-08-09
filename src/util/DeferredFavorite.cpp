#include "DeferredFavorite.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <deque>
#include <mutex>

#include "CrossPointState.h"

namespace DeferredFavorite {
namespace {

struct Job {
  std::string fromPath;
  std::string toPath;
};

struct Outcome {
  std::string fromPath;
  std::string toPath;
  bool ok;
};

// A press has to travel through a page repaint to reach this queue again, so the depth is
// only here to bound a stuck worker, never to hold a realistic burst.
constexpr size_t kMaxPending = 8;
// The worker opens no files and builds no documents; it walks directory entries through
// SdFat and renames. 4096 is the project's "does real card I/O" tier and leaves headroom
// for SdFat's own frames. It is transient: the task exits when the queue drains.
constexpr uint32_t kWorkerStackBytes = 4096;
constexpr UBaseType_t kWorkerPriority = 1;
constexpr uint32_t kIdlePollMs = 5;

// Function-local statics so construction happens on first use, not in an undefined
// static-init order across translation units.
std::mutex& stateMutex() {
  static std::mutex m;
  return m;
}

std::deque<Job>& pending() {
  static std::deque<Job> q;
  return q;
}

std::deque<Outcome>& finished() {
  static std::deque<Outcome> q;
  return q;
}

bool workerRunning = false;  // guarded by stateMutex()

// Card-only half of the favorite toggle. Deliberately does NOT call
// FavoriteImage::setFavorite: that one reads and writes APP_STATE, which belongs to the
// main task.
//
// Rename FIRST, diagnose only on failure. On FAT every name-based operation is a
// linear scan of the directory, seconds each with thousands of wallpapers, and each
// scan holds the storage mutex against the main task. The old exists/exists/rename
// order paid three scans on every success; SdFat never overwrites on rename, so the
// clash the pre-checks guarded against still fails safely — the two extra scans are
// only worth paying to label a failure that already happened.
bool renameOnCard(const Job& job) {
  if (Storage.rename(job.fromPath.c_str(), job.toPath.c_str())) return true;
  if (!Storage.exists(job.fromPath.c_str())) {
    LOG_ERR("DFAV", "%s is gone; cannot rename it", job.fromPath.c_str());
  } else if (Storage.exists(job.toPath.c_str())) {
    LOG_ERR("DFAV", "%s already exists; refusing to overwrite", job.toPath.c_str());
  } else {
    LOG_ERR("DFAV", "Rename of %s failed", job.fromPath.c_str());
  }
  return false;
}

void workerLoop(void*) {
  while (true) {
    Job job;
    {
      std::lock_guard<std::mutex> lock(stateMutex());
      if (pending().empty()) {
        // Cleared under the same lock request() checks it under, and the task exits
        // immediately after, so there is no window where a queued job has no worker.
        workerRunning = false;
        break;
      }
      job = std::move(pending().front());
      pending().pop_front();
    }

    const bool ok = renameOnCard(job);

    {
      std::lock_guard<std::mutex> lock(stateMutex());
      finished().push_back(Outcome{job.fromPath, job.toPath, ok});
    }
  }
  vTaskDelete(nullptr);
}

}  // namespace

bool request(const std::string& fromPath, const std::string& toPath) {
  if (fromPath == toPath) return true;  // already in the requested state

  // RAM only. Starting the worker here would begin directory scans on the card
  // while the reader is repainting and the user is turning pages — the mutex
  // contention this module exists to avoid. The job waits for flush().
  std::lock_guard<std::mutex> lock(stateMutex());
  // Toggling straight back cancels the queued toggle instead of stacking a
  // second rename: the two jobs are a net no-op on the card, and cancelling
  // keeps indecisive tapping from ever filling the queue.
  if (!pending().empty() && pending().back().fromPath == toPath && pending().back().toPath == fromPath) {
    pending().pop_back();
    return true;
  }
  if (pending().size() >= kMaxPending) {
    LOG_ERR("DFAV", "Favorite queue full; refusing %s", fromPath.c_str());
    return false;
  }
  pending().push_back(Job{fromPath, toPath});
  return true;
}

void flush() {
  std::lock_guard<std::mutex> lock(stateMutex());
  if (pending().empty() || workerRunning) return;

  // Started under the lock on purpose. Creating it after releasing would let a second
  // flush slip in, see workerRunning set, and trust a task that then failed to
  // start. xTaskCreate does not block on this mutex, so holding it here is safe.
  TaskHandle_t handle = nullptr;
  if (xTaskCreate(&workerLoop, "DeferredFav", kWorkerStackBytes, nullptr, kWorkerPriority, &handle) != pdPASS) {
    // Jobs stay queued; the next flush retries. waitForIdle() times out and its
    // callers already treat an undrained queue as "proceed and reconcile later".
    LOG_ERR("DFAV", "Could not start the favorite worker");
    return;
  }
  workerRunning = true;
}

void reconcile() {
  std::deque<Outcome> done;
  {
    std::lock_guard<std::mutex> lock(stateMutex());
    if (finished().empty()) return;
    done.swap(finished());
  }

  bool anySucceeded = false;
  for (const Outcome& outcome : done) {
    if (outcome.ok) {
      anySucceeded = true;
      continue;
    }
    // The name this rename was supposed to create does not exist, so put the reference
    // back before the next sleep tries to redraw it. Only while it is still the name we
    // advertised: the user may have toggled again, and that newer job owns it now.
    if (APP_STATE.lastSleepWallpaperPath == outcome.toPath) {
      APP_STATE.lastSleepWallpaperPath = outcome.fromPath;
    }
  }

  // One save for the batch. It persists whatever the reference now is, which is the point
  // of saving at all: an unexpected reset must not leave the card pointing at a name that
  // no longer exists. No sleepIndexDirty mark: a favorite rename keeps the folder's
  // membership — the index resolves the old record through favoriteCounterpart(), so
  // forcing a reconcile here only bought an "Indexing wallpapers" popup on the next boot.
  if (anySucceeded) {
    APP_STATE.saveToFile();
  }
}

bool isIdle() {
  std::lock_guard<std::mutex> lock(stateMutex());
  return pending().empty() && !workerRunning;
}

bool waitForIdle(const uint32_t timeoutMs) {
  flush();  // the queue cannot drain if no worker is running
  const uint32_t deadline = millis() + timeoutMs;
  while (!isIdle()) {
    // Signed difference so the comparison survives the millis() rollover.
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      LOG_ERR("DFAV", "Timed out waiting for the favorite worker");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kIdlePollMs));
  }
  return true;
}

}  // namespace DeferredFavorite
