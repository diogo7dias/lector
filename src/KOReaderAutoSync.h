#pragma once

#include <string>

// Auto Sync (KOReader settings): a book pulls its position once per wake when it
// opens, and the last book read pushes its position behind the sleep screen. Both
// run only with Auto Sync on and credentials set.
namespace ko_auto_sync {

// True the first time `path` opens since the last deep sleep. The claim survives the
// silent restart a sync ends with, so returning to the book never pulls again.
bool claimPull(const std::string& path);

// The reader left `path` on another page than it opened on: push it at the next sleep.
void markMoved(const std::string& path);

// Deep sleep entry, after the sleep screen is painted. Pushes the moved book over a
// saved network, blocking for at most a few joins, and arms a pull for the next wake.
void onSleep();

}  // namespace ko_auto_sync
