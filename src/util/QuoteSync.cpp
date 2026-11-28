#include "QuoteSync.h"

#include <ArduinoJson.h>
#include <ClockZones.h>
#include <Epub.h>
#include <Fnv1a.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <vector>

#include "ChapterXPathResolver.h"
#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"
#include "QuoteText.h"

namespace {

// BookOrbit serves KOSync under /api/v1/koreader and its plugin API beside it; any
// other KOSync server has no highlights to send to.
constexpr char BOOKORBIT_KOSYNC_SUFFIX[] = "/api/v1/koreader";
constexpr char ANNOTATIONS_PATH[] = "/plugin/annotations";
constexpr size_t MAX_PER_REQUEST = 50;     // the server's own cap
constexpr size_t FLUSH_AT_BYTES = 12000;   // keeps one request body small on the C3
constexpr size_t MAX_CHAPTER_BYTES = 500;  // the server rejects longer

struct Highlight {
  size_t textStart = 0;
  size_t textEnd = 0;
  std::string chapter;
  std::string note;
  std::string pos0;
  uint32_t savedAt = 0;
};

bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Cut at `max` bytes without splitting a UTF-8 character.
void clipUtf8(std::string& s, const size_t max) {
  if (s.size() <= max) return;
  size_t cut = max;
  while (cut > 0 && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) --cut;
  s.erase(cut);
}

bool readSidecar(const std::string& path, std::string& buf) {
  HalFile file;
  if (!Storage.openFileForRead("QSYNC", path, file)) return false;
  const size_t size = file.size();
  if (size == 0 || size > quote_text::MAX_QUOTES_FILE_BYTES) return false;
  buf.assign(size, '\0');
  return file.read(&buf[0], size) == static_cast<int>(size);
}

// Every quote with an anchor, its xpointer resolved while the book is loaded. Quotes
// saved before anchors existed are left out; ones saved before the page offset was
// recorded point at the start of their chapter.
std::vector<Highlight> resolve(Epub& epub, const std::string& buf) {
  std::vector<Highlight> out;
  out.reserve(16);
  int lastSpine = -1;
  uint32_t lastOffset = 0;
  std::string lastPos0;
  size_t pos = 0;
  quote_text::QuoteRecord record;
  while (quote_text::nextQuoteRecord(buf, pos, record)) {
    if (!record.hasHeader || record.textEnd <= record.textStart) continue;
    Highlight h;
    std::string token;
    quote_text::QuoteMeta meta;
    quote_text::splitChapterAnchor(buf.substr(record.headerStart, record.headerLen), h.chapter, token, &meta);
    quote_text::QuoteAnchor anchor;
    if (!quote_text::parseAnchorToken(token, anchor)) continue;
    // Each resolve parses the chapter: quotes from one page share the answer.
    if (anchor.spine != lastSpine || meta.textOffset != lastOffset) {
      lastSpine = anchor.spine;
      lastOffset = meta.textOffset;
      lastPos0 = meta.textOffset != quote_text::QuoteMeta::NO_OFFSET
                     ? ChapterXPathResolver::findXPathForVisibleTextOffset(epub, anchor.spine, meta.textOffset)
                     : ChapterXPathResolver::findXPathForProgress(epub, anchor.spine, 0.0f);
    }
    if (lastPos0.empty()) continue;
    h.pos0 = lastPos0;
    h.savedAt = meta.savedAt;
    h.textStart = record.textStart;
    h.textEnd = record.textEnd;
    if (record.hasNote && record.noteEnd > record.noteStart) {
      h.note = quote_text::sanitizeNote(buf.substr(record.noteStart, record.noteEnd - record.noteStart));
    }
    clipUtf8(h.chapter, MAX_CHAPTER_BYTES);
    out.push_back(std::move(h));
  }
  return out;
}

void addDevice(JsonDocument& doc) {
  doc["deviceId"] = "lector";
  doc["deviceModel"] = "Lector";
  doc["pluginVersion"] = "lector";
  // UTC throughout: BookOrbit keys a highlight on its time, so a time that moved with
  // the zone setting would send every quote again as new.
  if (const uint32_t now = quote_sync::nowUtc()) doc["deviceTime"] = quote_text::koreaderDatetime(now);
}

bool send(JsonDocument& doc) {
  std::string body;
  serializeJson(doc, body);
  const auto result = KOReaderSyncClient::postJson(ANNOTATIONS_PATH, body);
  LOG_INF("QSYNC", "Sent %u highlights: result=%d http=%d",
          static_cast<unsigned>(doc["books"][0]["annotations"].size()), result, KOReaderSyncClient::lastHttpCode);
  return result == KOReaderSyncClient::OK;
}

}  // namespace

namespace quote_sync {

uint32_t nowUtc() {
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0;
  if (!halClock.getDateTime(year, month, day, hour, minute)) return 0;
  // The clock reads to the minute; the seconds keep two saves in one minute apart.
  // ponytail: seconds from uptime, not the clock; enough to tell two saves apart.
  return static_cast<uint32_t>(clock_zones::utcSeconds(year, month, day, hour, minute) + (millis() / 1000) % 60);
}

int64_t toLocal(const uint32_t utc) {
  return static_cast<int64_t>(utc) +
         static_cast<int64_t>(clock_zones::offsetMinutes(SETTINGS.clockTimezone, SETTINGS.clockDst, utc)) * 60;
}

bool push(const std::string& bookPath, const std::string& documentHash) {
  const std::string baseUrl = KOREADER_STORE.getBaseUrl();
  if (!endsWith(baseUrl, BOOKORBIT_KOSYNC_SUFFIX)) return true;
  const std::string path = quote_text::quotesFilePathFor(bookPath);
  if (!Storage.exists(path.c_str())) return true;
  std::string buf;
  if (!readSidecar(path, buf)) return false;

  auto epub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
  if (!epub) {
    LOG_ERR("QSYNC", "OOM: Epub");
    return false;
  }
  epub->setupCacheDir();
  if (!epub->load(false, true)) {
    LOG_ERR("QSYNC", "Cannot load %s", bookPath.c_str());
    return false;
  }
  // The quotes as last sent to this server: unchanged means nothing new to say, so no
  // chapter parsing and no TLS behind the sleep face.
  const uint32_t sent = fnv1a::hash32(documentHash, fnv1a::hash32(baseUrl, fnv1a::hash32(buf)));
  const std::string sentPath = epub->getCachePath() + "/quotes_sent.bin";
  if (Storage.exists(sentPath.c_str())) {
    HalFile in;
    uint32_t lastSent = 0;
    if (Storage.openFileForRead("QSYNC", sentPath, in) && in.read(&lastSent, sizeof(lastSent)) == sizeof(lastSent) &&
        lastSent == sent) {
      LOG_DBG("QSYNC", "Quotes unchanged since the last send");
      return true;
    }
  }
  const std::vector<Highlight> highlights = resolve(*epub, buf);
  epub.reset();  // gone before the first request, so the TLS handshake gets its heap
  if (highlights.empty()) return true;

  JsonDocument doc;
  JsonArray annotations;
  const auto start = [&]() {
    doc.clear();
    addDevice(doc);
    JsonObject book = doc["books"].to<JsonArray>().add<JsonObject>();
    book["hash"] = documentHash;
    annotations = book["annotations"].to<JsonArray>();
  };
  start();
  bool ok = true;
  for (const auto& h : highlights) {
    if (annotations.size() >= MAX_PER_REQUEST || measureJson(doc) > FLUSH_AT_BYTES) {
      ok = send(doc) && ok;
      start();
    }
    std::string text = buf.substr(h.textStart, h.textEnd - h.textStart);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    JsonObject a = annotations.add<JsonObject>();
    a["datetime"] = quote_text::koreaderDatetime(h.savedAt ? h.savedAt : quote_text::fallbackSavedAt(text));
    a["drawer"] = "lighten";
    a["text"] = text;
    if (!h.note.empty()) a["note"] = h.note;
    if (!h.chapter.empty()) a["chapter"] = h.chapter;
    a["posFormat"] = "xpointer";
    a["pos0"] = h.pos0;
  }
  if (annotations.size() > 0) ok = send(doc) && ok;
  if (ok) {
    HalFile out;
    if (Storage.openFileForWrite("QSYNC", sentPath, out)) out.write(&sent, sizeof(sent));
  }
  return ok;
}

}  // namespace quote_sync
