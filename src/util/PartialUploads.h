#pragma once

#include <string>
#include <string_view>

/**
 * Housekeeping for the ".part" files an interrupted transfer leaves behind.
 *
 * A partial is kept on purpose: it is what the next attempt resumes from. What
 * it must not do is accumulate, because nothing else ever deletes it and an SD
 * card full of half-books is a worse failure than a slow upload. The bound is
 * one partial per folder: starting a transfer into a folder clears every other
 * partial there.
 *
 * One is enough because a resume only ever picks up the transfer that just
 * stopped. Age would be the other way to bound this, but the device has no clock
 * it can trust for it, and a count needs no state file to go stale.
 */
namespace partial_uploads {

/**
 * Removes every ".part" file directly inside `folder` except `keepPath`, which
 * is the partial the caller is about to write to (pass an empty view to clear
 * them all). `folder` is the card root when empty. Returns how many were
 * removed.
 */
int sweepFolder(std::string_view folder, std::string_view keepPath);

}  // namespace partial_uploads
