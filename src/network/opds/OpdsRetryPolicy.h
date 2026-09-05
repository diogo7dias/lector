#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Reconnection and retry policy for OPDS operations, kept free of Arduino,
// TLS, and UI headers so it can be reasoned about and unit-tested in isolation.
// When an OPDS server reboots or becomes temporarily unavailable, the client
// retries with exponential backoff rather than failing immediately or freezing.
namespace opds_retry {

// Maximum number of attempts for an OPDS connection before reporting failure.
constexpr int MAX_ATTEMPTS = 3;

// Default timeout for an OPDS request in milliseconds (15 seconds).
// Significantly shorter than general large downloads (60s) so the client can
// detect a down/rebooting server promptly and report reconnection state to the user.
constexpr uint32_t DEFAULT_TIMEOUT_MS = 15000;

// Initial exponential backoff delay in milliseconds.
constexpr unsigned long INITIAL_BACKOFF_MS = 1000;

// Maximum exponential backoff delay in milliseconds (capped at 8 seconds).
constexpr unsigned long MAX_BACKOFF_MS = 8000;

// Returns true if the HTTP status code indicates a temporary server issue
// (e.g. during a server reboot or restart) that is worth retrying.
inline bool isRetryableHttpStatus(const int status) {
  // 0: Connection dropped, timeout, TCP connection refused / host unreachable
  if (status == 0) return true;
  // 408: Request Timeout
  if (status == 408) return true;
  // 429: Too Many Requests (transient rate limiting)
  if (status == 429) return true;
  // 500: Internal Server Error (often temporary while server is starting up or database warming)
  // 502: Bad Gateway (common when reverse proxy like Nginx/Caddy is up but backend OPDS server is rebooting)
  // 503: Service Unavailable (server restarting/maintenance)
  // 504: Gateway Timeout (backend server taking too long during restart)
  if (status == 500 || status == 502 || status == 503 || status == 504) return true;

  // 401 (bad credentials), 403 (forbidden), 404 (not found), etc. are permanent failures
  return false;
}

// Determines whether a failed attempt should be retried.
// attemptsMade: 1 for the first attempt, 2 for the second, etc.
inline bool shouldRetry(const int httpStatus, const int attemptsMade, const int maxAttempts = MAX_ATTEMPTS) {
  if (attemptsMade >= maxAttempts) return false;
  return isRetryableHttpStatus(httpStatus);
}

// Computes the exponential backoff delay in milliseconds for the given attempt.
// attempt 1 (after 1st failure): initialMs (e.g. 1000ms)
// attempt 2 (after 2nd failure): initialMs * 2 (e.g. 2000ms)
// attempt 3 (after 3rd failure): initialMs * 4 (e.g. 4000ms)
// Capped at maxMs (e.g. 8000ms).
inline unsigned long backoffMs(const int attemptsMade, const unsigned long initialMs = INITIAL_BACKOFF_MS,
                               const unsigned long maxMs = MAX_BACKOFF_MS) {
  if (attemptsMade <= 0) return 0;
  const unsigned long multiplier = (attemptsMade <= 30) ? (1UL << (attemptsMade - 1)) : (1UL << 30);
  const unsigned long delay = initialMs * multiplier;
  return (delay < initialMs || delay > maxMs) ? maxMs : delay;
}

}  // namespace opds_retry
