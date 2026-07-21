#pragma once
#include <cstdint>
#include <string>

// Document matching method for KOReader sync
enum class DocumentMatchMethod : uint8_t {
  FILENAME = 0,  // Match by filename (simpler, works across different file sources)
  BINARY = 1,    // Match by partial MD5 of file content (more accurate, but files must be identical)
};

// How manual "Sync Progress" resolves differences after fetching remote progress.
enum class KOReaderSyncBehavior : uint8_t {
  ASK_EVERY_TIME = 0,  // Always show the Apply/Upload choice (legacy behavior).
  SMART = 1,           // Auto-resolve: upload when local is ahead, apply when remote is ahead,
                       // do nothing when already in sync, upload when no remote exists.
};

/**
 * Singleton class for storing KOReader sync credentials on the SD card.
 * Passwords are XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON (not cryptographically secure,
 * but prevents casual reading and ties credentials to the specific device).
 */
class KOReaderCredentialStore {
 private:
  static KOReaderCredentialStore instance;
  std::string username;
  std::string password;
  std::string serverUrl;                                            // Custom sync server URL (empty = default)
  DocumentMatchMethod matchMethod = DocumentMatchMethod::FILENAME;  // Default to filename for compatibility
  // New stores default to SMART; stores migrated from an older file without this
  // key are set to ASK_EVERY_TIME on load so existing users keep manual confirm.
  KOReaderSyncBehavior syncBehavior = KOReaderSyncBehavior::SMART;
  bool sendMetadata = false;  // Send document title/author/filename with progress uploads

  // Private constructor for singleton
  KOReaderCredentialStore() = default;

  bool loaded = false;

  // Lazy first-use load: main.cpp no longer reads this store at boot, so every
  // public entry point funnels through here before touching the fields.
  void ensureLoaded() const {
    if (!loaded) const_cast<KOReaderCredentialStore*>(this)->loadFromFile();
  }

  bool loadFromBinaryFile();

 public:
  // Delete copy constructor and assignment
  KOReaderCredentialStore(const KOReaderCredentialStore&) = delete;
  KOReaderCredentialStore& operator=(const KOReaderCredentialStore&) = delete;

  // Get singleton instance
  static KOReaderCredentialStore& getInstance() { return instance; }

  // Save/load from SD card
  bool saveToFile() const;
  bool loadFromFile();

  // Credential management
  void setCredentials(const std::string& user, const std::string& pass);
  const std::string& getUsername() const {
    ensureLoaded();
    return username;
  }
  const std::string& getPassword() const {
    ensureLoaded();
    return password;
  }

  // Get MD5 hash of password for API authentication
  std::string getMd5Password() const;

  // Check if credentials are set
  bool hasCredentials() const;

  // Clear credentials
  void clearCredentials();

  // Server URL management
  void setServerUrl(const std::string& url);
  const std::string& getServerUrl() const {
    ensureLoaded();
    return serverUrl;
  }

  // Get base URL for API calls (with http:// normalization if no protocol, falls back to default)
  std::string getBaseUrl() const;

  // Document matching method
  void setMatchMethod(DocumentMatchMethod method);
  DocumentMatchMethod getMatchMethod() const {
    ensureLoaded();
    return matchMethod;
  }

  // Sync behavior (Ask every time vs Smart auto-resolve)
  void setSyncBehavior(KOReaderSyncBehavior behavior);
  KOReaderSyncBehavior getSyncBehavior() const {
    ensureLoaded();
    return syncBehavior;
  }

  // Send document metadata (title/author/filename) with progress uploads
  void setSendMetadata(bool enabled);
  bool getSendMetadata() const {
    ensureLoaded();
    return sendMetadata;
  }
};

// Helper macro to access credential store
#define KOREADER_STORE KOReaderCredentialStore::getInstance()
