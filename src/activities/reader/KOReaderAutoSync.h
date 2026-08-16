#pragma once

// Unattended KOReader sync: the network half of automatic two-way progress sync.
//
// Everything here blocks. There is no activity, no button hints and no network
// picker: these calls run at moments where the user has already committed to
// something else (locking the device, closing a book, opening one), so they take
// the radio, do one request and give it back.
//
// The three call sites are:
//
//   1. Locking with the power button, from the reader (main.cpp, before deep
//      sleep). Pushes. Deep sleep resets the chip, so no cleanup is owed.
//   2. Leaving a book for the library (EpubReaderActivity). Pushes, then the
//      caller silent-restarts to drop the heap fragmentation TLS leaves behind.
//   3. Opening a book (main.cpp, at boot). Fetches the remote position, stashes
//      it in RTC memory, and silent-restarts into the reader. The reader then
//      resolves that XPath with the EPUB it was going to load anyway, so the
//      book is never parsed twice and TLS never runs next to a loaded book.
//
// Whether any of this may run at all is decided by KOReaderAutoSyncPolicy.h.

#include <cstdint>
#include <optional>
#include <string>

#include "KOReaderAutoSyncPolicy.h"
#include "ProgressMapper.h"

namespace KOReaderAutoSync {

// A reading position captured while the EPUB was still in RAM, so the push path
// needs nothing but the network.
struct Snapshot {
  std::string epubPath;
  SavedProgressPosition position;  // XPath plus percentage
  std::string title;               // Empty unless the metadata setting is on
  std::string authors;             // Empty unless the metadata setting is on
  int spineIndex = 0;
  int pageNumber = 0;
  int totalPagesInSpine = 1;
  std::optional<uint16_t> paragraphIndex;  // First paragraph visible on the page
};

// The device's current answer to "may auto sync run?", read from the credential
// stores. Kept separate from the policy so the policy stays host-testable.
ko_auto_sync::Gate currentGate();

// Join a saved network, preferring the last one used. Returns false on timeout
// or when nothing is saved. Never prompts.
//
// The default budget is deliberately short. This runs in front of a user who is waiting
// for a page, so failing fast and opening the book beats joining a marginal network.
bool connectSavedWifi(uint32_t timeoutMs = 8000);

// Shut the radio down. Callers that stay running afterwards must silent-restart:
// stopping WiFi does not give back the heap TLS fragmented.
void stopWifi();

// Push a position to the sync server. Assumes WiFi is already up. Returns true
// only when the server accepted the upload.
bool pushProgress(const Snapshot& snapshot);

// Fetch the remote position for a book and store it for the reader to apply after
// a reboot. Whether it is actually further along than where this device already is
// can only be judged once the EPUB is open, so that comparison belongs to the
// reader, not here. Returns true when something was stored.
bool fetchAndStashRemote(const std::string& epubPath);

// Take the position stashed by fetchAndStashRemote for this book, clearing it so a later
// boot cannot apply it twice. Returns nothing when no valid handoff is waiting, or when
// the one waiting belongs to a different book.
std::optional<ko_auto_sync::PendingPull> takePendingPull(const std::string& epubPath);

// Is a fetched position for this book waiting to be applied? Lets the book-open path
// tell a first open from the reboot its own fetch asked for, without consuming the
// handoff. A stash left over for a different book does not count, or that book's
// leftovers would block this one from ever fetching.
bool hasPendingPullFor(const std::string& epubPath);

// Drop any waiting handoff without applying it (e.g. a different book opened).
void clearPendingPull();

// False once this book has already been pulled during this wake. Stepping out to the
// library and back into the same book cannot have changed anything on the other device,
// so the second open goes straight to the page.
bool pullIsWorthMaking(const std::string& epubPath);

// Record that this book was pulled, so the next open of it this wake is instant.
void notePullMade(const std::string& epubPath);

// Forget which books were pulled. Called when the device locks: after a lock the other
// device may have moved, so every book is owed a fresh pull.
void forgetPullsOnLock();

}  // namespace KOReaderAutoSync
