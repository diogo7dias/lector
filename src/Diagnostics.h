#pragma once

#include <cstddef>
#include <cstdint>

// The on-card diagnostics file, /lector-flash-diagnostics.txt.
//
// Why a file: a USB-locked reader gives no serial console, so the only way to
// see why an update failed is to have the firmware that ran it write down what
// it saw. The file is one bounded text file a user can download from the web
// file browser (or copy off the card) and paste whole into a chat.
//
// What it holds: firmware install attempts from the SD card and over the air,
// with the partition table, both otadata records, battery and heap at the
// start, where each stage stopped and at which byte; the OTA heap gates; boots
// after a crash, watchdog or brownout; the boot after an install, which is the
// answer to "did the update take"; and a card that would not mount.
//
// What it never holds: book titles, paths inside the library, Wi-Fi names or
// addresses, server URLs, serial numbers. A firmware image path is reduced to
// its name when it sits at the card root and to "(file in a folder)" otherwise.
//
// Wi-Fi join checkpoints also record session/radio progress before OTA starts.
// Cost: recording is a memcpy into a 2 KB static buffer (lib/DiagLog). The card
// is written only by flush(), which runs when an attempt ends, an update
// fails, an abnormal boot is seen, the file is downloaded, or the reader locks
// with something still unflushed. Wi-Fi adds at most 48 immediate checkpoints
// per screen entry, then one per 5 seconds. A page turn, wake or render never
// calls in.
//
// Every value printed is measured at the moment it is printed, or says
// "unknown". The old file printed a placeholder size=0 for an image it had not
// opened yet, and that one line sent a debugging session after an empty file
// the user never had.
namespace diag {

constexpr const char* kFilePath = "/lector-flash-diagnostics.txt";
constexpr size_t kUnknownSize = ~static_cast<size_t>(0);

enum class Source : uint8_t { Sd, Ota };

// Opens an attempt entry: image, battery, heap, running slot, partition table
// and both otadata records. `imageSize` is kUnknownSize when the file has not
// been measured; it prints as "unknown", never as 0. Marks the buffer pending.
void beginAttempt(Source source, const char* imagePath, size_t imageSize);

// One indented line under the current entry.
void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Battery and heap as they are right now, one line. beginAttempt records
// them once; a readback failure records them again, because a cell that sagged
// during a 4 MB write is the fault.
void noteVitals();

// After ota_boot::switchTo: the destination and both otadata records again.
void recordSwitch(uint32_t destAddress, const char* destLabel, uint8_t destSubtype, bool switchOk);

// Closes the attempt with the same result name the failure screen shows, and
// the stage it stopped at (null on success). Flushes.
void endAttempt(const char* result, const char* stage);

// The boot that follows an install: which slot was meant to run, which does.
// Marks pending; main() flushes once after all boot records.
void recordBootAfterInstall(uint32_t intendedAddress, uint32_t runningAddress, uint32_t imageSize);

// A boot whose reset reason is a crash, watchdog or brownout. Anything else
// records nothing, so an ordinary wake costs no card write. Marks pending.
void recordAbnormalBoot();

// The card did not mount at boot. Held in RAM; reaches the card only if a later
// mount succeeds and something flushes.
void recordSdMountFailure();

// An update failure as the screen reports it: `step` is "check" or "install",
// `error` the OtaUpdaterError name, `screenLine` the detail line drawn under
// the message (numbers and chip names only). Flushes.
void recordOtaFailure(const char* step, const char* error, const char* screenLine);

// A TLS heap gate: the numbers the reader would otherwise have to read off
// the screen. Informational: it reaches the card only if a failure follows.
void recordTlsGate(const char* step, uint32_t freeHeap, uint32_t largestBlock, bool framebufferLent, uint32_t floorFree,
                   uint32_t floorBlock, bool allowed, uint32_t poolFree, uint32_t poolBlock);

// RAM only. Signed heap delta includes allocator overhead and concurrent heap
// activity; vector payload is separately calculated from capacity * element size.
void recordHeapReclaim(const char* step, uint32_t before, uint32_t after, size_t capacity = 0, size_t elementSize = 0);

// Writes the buffer to the card, applying retention (lib/DiagLog): header,
// surviving old entries, then the buffered lines. No-op when nothing is
// buffered or the card is not mounted.
void flush();
// Starts a retained Wi-Fi checkpoint entry in the SAME buffer/file. RAM only.
void beginWifiCheckpoint();
// flush() only when something worth keeping was recorded. The lock path and
// the web download call this.
void flushIfPending();

}  // namespace diag
