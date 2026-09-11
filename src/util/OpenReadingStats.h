#pragma once

#include <cstdint>
#include <string>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace reading_stats {
class ReaderStatsSession;
}

// Opens BookStatsActivity for the most recent book, read off the card. Same screen
// Home Back already uses. False when there is no recent book, or its type has no cache.
bool launchRecentBookStats(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput);

// Opens BookStatsActivity from a live reader session. Pauses the tracker first so time
// on the stats screen is not counted as reading.
void launchLiveReadingStats(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput,
                            reading_stats::ReaderStatsSession& session, bool trackingActive, const std::string& title,
                            uint8_t progressPercent);
