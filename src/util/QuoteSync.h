#pragma once

#include <cstdint>
#include <string>

// Saved quotes as BookOrbit highlights, sent with each KOReader position sync.
namespace quote_sync {

// Now as UTC seconds, 0 when the device has no clock it trusts.
uint32_t nowUtc();

// UTC seconds as local wall-clock seconds, in the zone the clock settings name.
int64_t toLocal(uint32_t utc);

// Sends every quote saved for the book to BookOrbit (one-way, add only: the server
// keys a highlight on its time and position, so a resend changes nothing and a
// highlight removed on the server stays removed). No-op, returning true, when the
// KOSync server is not BookOrbit or the book has no quotes. Needs Wi-Fi up.
bool push(const std::string& bookPath, const std::string& documentHash);

}  // namespace quote_sync
