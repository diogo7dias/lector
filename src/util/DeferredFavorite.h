#pragma once

#include <cstdint>
#include <string>

// Defers the sleep-wallpaper favorite rename off the press entirely.
//
// Why this exists: favoriting is a rename on the SD card, and on a FAT card every
// name-based operation is a linear scan of the directory — with thousands of
// wallpapers, seconds per scan. Running the rename "in the background" at press
// time is not enough: every scan holds the storage mutex, so the page repaint and
// the page turns pressed right after queue behind it and the reader feels stuck.
//
// So request() does NO card work at all — it only records the job in RAM. The
// work runs when flush() is called, at moments where seconds of card traffic are
// already expected and cannot steal a page turn:
//   - leaving the reader (EpubReaderActivity::onExit — going home / changing book)
//   - entering sleep (SleepActivity::onEnter — the lock)
// flush() spawns a worker task that drains the queue and deletes itself, so the
// idle cost is a flag and an empty deque, not a permanently parked stack.
//
// It is a task and not a job queued onto the main loop on purpose: the main loop
// is what polls the buttons, so parking SD I/O on it would drop the page turn
// pressed right after. See the locked no-dropped-press rule.
//
// DIVISION OF LABOUR, and the reason for it: the worker touches files and nothing
// else. APP_STATE belongs to the main task alone. std::string assignment is not
// atomic, so a worker writing lastSleepWallpaperPath while the reader reads it
// would be a data race on a live object. The worker therefore records what
// happened, and reconcile() applies it from the main task.
namespace DeferredFavorite {

// Queues renaming `fromPath` to `toPath` on the card. RAM only — no SD access,
// no task spawn, returns immediately. The rename happens at the next flush().
//
// The CALLER is expected to have already moved APP_STATE.lastSleepWallpaperPath
// to `toPath` (FavoriteImage::favoritePathFor derives it), so the UI can move on
// without waiting. If the rename then fails, the next reconcile() puts that
// reference back.
//
// Returns false when the queue is full; the caller should then do the work in
// the foreground instead of dropping the press.
bool request(const std::string& fromPath, const std::string& toPath);

// Starts draining the queue on the worker task. Returns immediately; pair with
// waitForIdle() when the caller must see the renames on the card. No-op when
// nothing is queued. Safe to call from the main task at any natural pause —
// but only at moments where card contention cannot steal a page turn.
void flush();

// MAIN TASK ONLY. Applies finished jobs to APP_STATE: restores the reference for
// any rename that failed, and saves the state once if any rename succeeded.
// Cheap and does nothing when no job has finished, so it is safe to call at any
// natural pause.
void reconcile();

// True when nothing is queued and no worker is running.
bool isIdle();

// The name a queued rename will give `fromPath`, or an empty string when nothing is
// queued for it. Callers that draw a file name need this: between the press and the
// flush the card still holds the OLD name, so a listing that reads the card alone shows
// a file the user has just favorited as not favorited, which looks like the press was
// lost. The queue is a handful of entries, so this is a linear scan and is cheap enough
// to call once per drawn row.
std::string pendingTargetFor(const std::string& fromPath);

// flush() + block until the queue drains, or until `timeoutMs` elapses. Returns
// true if it drained. Call this before anything that resets the chip (deep
// sleep), so a queued rename is never lost while the name that assumes it
// happened is already live — and before any foreground operation on the
// wallpaper the queue may still be renaming (pause, delete).
bool waitForIdle(uint32_t timeoutMs);

}  // namespace DeferredFavorite
