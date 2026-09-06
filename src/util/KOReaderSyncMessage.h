#pragma once

#include <I18n.h>

#include "KOReaderSyncClient.h"

/**
 * Reader-facing text for a sync outcome. KOReaderSyncClient::errorString stays
 * English for the log; anything shown on the screen goes through here, because
 * no library under lib/ can reach the translation tables.
 */
inline const char* koSyncErrorText(const KOReaderSyncClient::Error error) {
  switch (error) {
    case KOReaderSyncClient::OK:
      return tr(STR_UPLOAD_SUCCESS);
    case KOReaderSyncClient::NO_CREDENTIALS:
      return tr(STR_NO_CREDENTIALS_MSG);
    case KOReaderSyncClient::NETWORK_ERROR:
      return tr(STR_SYNC_NETWORK_ERROR);
    case KOReaderSyncClient::AUTH_FAILED:
      return tr(STR_AUTH_FAILED);
    case KOReaderSyncClient::SERVER_ERROR:
      return tr(STR_SYNC_SERVER_ERROR);
    case KOReaderSyncClient::JSON_ERROR:
      return tr(STR_SYNC_BAD_RESPONSE);
    case KOReaderSyncClient::NOT_FOUND:
      return tr(STR_NO_REMOTE_MSG);
    case KOReaderSyncClient::LOW_MEMORY:
      return tr(STR_UPDATE_LOW_MEMORY);
    case KOReaderSyncClient::USER_EXISTS:
      return tr(STR_USERNAME_TAKEN);
  }
  return tr(STR_SYNC_FAILED_MSG);
}
