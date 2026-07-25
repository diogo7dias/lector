#include "OpdsServerStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>

namespace {

// Servers this firmware ships with. Name and URL only — see seedBuiltInServers.
struct BuiltInServer {
  const char* name;
  const char* url;
};

constexpr BuiltInServer BUILT_IN_SERVERS[] = {
    // Calibre-Web's OPDS feed. The scheme is spelled out because UrlUtils::ensureProtocol
    // defaults a bare host to http://, and this host answers on HTTPS only — a plain
    // request to its TLS port fails rather than redirecting.
    {"Kumedia Library", "https://kumedia.duckdns.org:8450/opds"},
};

}  // namespace

void OpdsServerStore::toJson(JsonDocument& doc) const {
  doc["builtins_seeded"] = builtInsSeeded;
  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& server : servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = server.name;
    obj["url"] = server.url;
    obj["username"] = server.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(server.password);
  }
}

bool OpdsServerStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'servers' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  servers.clear();
  builtInsSeeded = doc["builtins_seeded"] | false;
  JsonArrayConst arr = doc["servers"].as<JsonArrayConst>();
  servers.reserve(std::min(arr.size(), MAX_SERVERS));
  bool needsResave = false;

  for (JsonObjectConst obj : arr) {
    if (servers.size() >= OpdsServerStore::MAX_SERVERS) break;
    OpdsServer server;
    server.name = obj["name"] | "";
    server.url = obj["url"] | "";
    server.username = obj["username"] | "";
    server.password = extractPassword(obj, needsResave);
    servers.push_back(std::move(server));
  }

  LOG_DBG("OPS", "Loaded %zu OPDS servers from file", servers.size());

  if (needsResave) {
    LOG_DBG("OPS", "Resaving JSON with obfuscated passwords");
    requestResave();
  }

  return true;
}

void OpdsServerStore::seedBuiltInServers() {
  if (builtInsSeeded) return;
  builtInsSeeded = true;

  for (const BuiltInServer& builtIn : BUILT_IN_SERVERS) {
    if (servers.size() >= MAX_SERVERS) break;
    // Skip one the user already added by hand, so seeding cannot double it up.
    const bool alreadyPresent =
        std::any_of(servers.begin(), servers.end(), [&](const OpdsServer& s) { return s.url == builtIn.url; });
    if (alreadyPresent) continue;

    OpdsServer server;
    server.name = builtIn.name;
    server.url = builtIn.url;  // username and password are left empty on purpose
    servers.push_back(std::move(server));
    LOG_INF("OPS", "Seeded built-in OPDS server: %s", builtIn.name);
  }

  saveToFile();
}

bool OpdsServerStore::addServer(const OpdsServer& server) {
  if (servers.size() >= MAX_SERVERS) {
    LOG_DBG("OPS", "Cannot add more servers, limit of %zu reached", MAX_SERVERS);
    return false;
  }

  servers.push_back(server);
  LOG_DBG("OPS", "Added server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::updateServer(size_t index, const OpdsServer& server) {
  if (index >= servers.size()) {
    return false;
  }

  servers[index] = server;
  LOG_DBG("OPS", "Updated server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::removeServer(size_t index) {
  if (index >= servers.size()) {
    return false;
  }

  LOG_DBG("OPS", "Removed server: %s", servers[index].name.c_str());
  servers.erase(servers.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const OpdsServer* OpdsServerStore::getServer(size_t index) const {
  if (index >= servers.size()) {
    return nullptr;
  }
  return &servers[index];
}
