#pragma once

#include "ReadingJournal.h"

// Card side of the reading journal (format and folding live in ReadingJournal.h).
namespace reading_journal {

// Local wall time, when the clock has been set (RTC, or an NTP sync since power-on).
bool localNow(Civil& out);

// Ends the session at nowMs and appends its line, unless it was under MIN_SESSION_S.
void append(Session& session, uint32_t nowMs, const char* title);

// The sitting the last append() call closed, this boot: false when that sitting was too
// short to keep, or none has closed. The sleep band reads it to show the sitting just ended.
bool lastSitting(uint32_t& seconds, uint32_t& pages);

// Folds the journal into `summary`, with today set when the clock is. tailBytes > 0 reads
// only the file's last tailBytes (the sleep line needs one week, not years). False when
// there is no journal yet.
bool summarize(Summary& summary, size_t tailBytes = 0);

// Folds every journal line under history.title. False when there is no journal yet.
bool bookHistory(BookHistory& history);

}  // namespace reading_journal
