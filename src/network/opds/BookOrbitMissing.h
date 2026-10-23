#pragma once

#include <OpdsParser.h>

#include <string>
#include <vector>

#include "OpdsServerStore.h"

// Books marked Reading on BookOrbit that this card does not hold. The sleep check
// (Auto Sync) fetches the server's "Reading" smart shelf and saves how many are
// missing; Home shows the count, and the browser lists only those books.
namespace bookorbit_missing {

// The first OPDS server that is a BookOrbit catalogue with an account on it.
const OpdsServer* findServer();

// True when the card already holds `book`: the file an OPDS download would write
// exists, or a Recents book with the same title is still on the card.
bool onDevice(const OpdsEntry& book);

size_t countBooks(const std::vector<OpdsEntry>& entries);

// Drops the books the card already holds; returns how many are left.
size_t keepMissing(std::vector<OpdsEntry>& entries);

// Missing count and the Reading feed URL from the last check; 0 and empty when none.
int savedCount();
std::string savedFeedUrl();
void save(int count, const std::string& feedUrl);

// Whether the sleep check runs: always when WiFi is already up for a push,
// otherwise once every 6 hours, and never on a clock that was never set.
bool due(bool wifiUp);

// Fetches the Reading shelf over the WiFi already up and saves the result.
void refresh();

}  // namespace bookorbit_missing
