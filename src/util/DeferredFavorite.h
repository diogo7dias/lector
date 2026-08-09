#pragma once

#include <cstdint>
#include <string>

// Runs the sleep-wallpaper favorite rename off the main task.
//
// Why this exists: favoriting is a rename plus a state save on the SD card, and the
// in-book menu used to wait for all of it before anything could redraw. The reader now
// closes the menu and repaints the page immediately, and the card work happens here,
// underneath the panel refresh the page repaint was going to cost anyway.
//
// It is a task and not a job queued onto the main loop on purpose: the main loop is what
// polls the buttons, so parking SD I/O on it would drop the page turn pressed right after
// the favorite. See the locked no-dropped-press rule.
//
// The worker is spawned on demand and deletes itself once the queue drains, so the idle
// cost is a flag and an empty deque, not a permanently parked stack.
//
// DIVISION OF LABOUR, and the reason for it: the worker touches files and nothing else.
// APP_STATE belongs to the main task alone. std::string assignment is not atomic, so a
// worker writing lastSleepWallpaperPath while the reader reads it would be a data race on
// a live object. The worker therefore records what happened, and reconcile() applies it
// from the main task.
namespace DeferredFavorite {

// Queues renaming `fromPath` to `toPath` on the card.
//
// The CALLER is expected to have already moved APP_STATE.lastSleepWallpaperPath to
// `toPath` (FavoriteImage::favoritePathFor derives it), so the UI can move on without
// waiting. If the rename then fails, the next reconcile() puts that reference back.
//
// Returns false when the queue is full or the worker could not be started; the caller
// should then do the work in the foreground instead of dropping the press.
bool request(const std::string& fromPath, const std::string& toPath);

// MAIN TASK ONLY. Applies finished jobs to APP_STATE: restores the reference for any
// rename that failed, and saves the state once if any rename succeeded. Cheap and does
// nothing when no job has finished, so it is safe to call at any natural pause.
void reconcile();

// True when nothing is queued and no worker is running.
bool isIdle();

// Blocks the calling task until the queue drains, or until `timeoutMs` elapses. Returns
// true if it drained. Call this before anything that resets the chip (deep sleep), so a
// queued rename is never lost while the name that assumes it happened is already live.
bool waitForIdle(uint32_t timeoutMs);

}  // namespace DeferredFavorite
