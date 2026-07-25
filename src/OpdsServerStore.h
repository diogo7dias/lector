#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct OpdsServer {
  std::string name;
  std::string url;
  std::string username;
  std::string password;  // Plaintext in memory; obfuscated with hardware key on disk
};

/**
 * Singleton class for storing OPDS server configurations on the SD card.
 * Passwords are XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON.
 */
class OpdsServerStore : public PersistableStore<OpdsServerStore> {
 private:
  std::vector<OpdsServer> servers;

  static constexpr size_t MAX_SERVERS = 8;

  OpdsServerStore() = default;

  friend class PersistableStore<OpdsServerStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/opds.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool addServer(const OpdsServer& server);
  bool updateServer(size_t index, const OpdsServer& server);
  bool removeServer(size_t index);

  const std::vector<OpdsServer>& getServers() const { return servers; }
  const OpdsServer* getServer(size_t index) const;
  size_t getCount() const { return servers.size(); }
  bool hasServers() const { return !servers.empty(); }

  // Adds the servers this firmware ships with, once, so a fresh card arrives with
  // the library already listed instead of the user typing a URL on a five-button
  // keyboard. Only their name and URL are built in — NEVER credentials, because
  // this firmware is published as a public binary and anything compiled into it is
  // readable by anyone who downloads it. The user fills in username and password on
  // the device, and those stay on their own card, obfuscated like any other entry.
  //
  // Runs only when the marker in the file is absent, so a server the user deletes
  // stays deleted and a re-flash does not resurrect it.
  void seedBuiltInServers();

 private:
  // Set once seedBuiltInServers has run, and persisted with the list.
  bool builtInsSeeded = false;
};

#define OPDS_STORE OpdsServerStore::getInstance()
