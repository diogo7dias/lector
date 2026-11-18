#pragma once

#include "ReadingJournal.h"

// Card side of the reading journal (format and folding live in ReadingJournal.h).
namespace reading_journal {

// Local wall time, when the clock has been set (RTC, or an NTP sync since power-on).
bool localNow(Civil& out);

// Ends the session at nowMs and appends its line, unless it was under MIN_SESSION_S.
void append(Session& session, uint32_t nowMs, const char* title);

// Folds the journal into `summary`, with today set when the clock is. False when
// there is no journal yet.
bool summarize(Summary& summary);

// Folds every journal line under history.title. False when there is no journal yet.
bool bookHistory(BookHistory& history);

}  // namespace reading_journal
