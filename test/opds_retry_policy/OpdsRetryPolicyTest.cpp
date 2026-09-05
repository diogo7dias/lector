#include <gtest/gtest.h>

#include "network/opds/OpdsRetryPolicy.h"

using namespace opds_retry;

TEST(OpdsRetryPolicy, ConnectionDropOrServerDownIsRetryable) {
  // 0 status means connection refused, timeout, or network down (server rebooting)
  EXPECT_TRUE(isRetryableHttpStatus(0));
}

TEST(OpdsRetryPolicy, GatewayAndServerRebootErrorsAreRetryable) {
  // Reverse proxy errors when backend OPDS server reboots
  EXPECT_TRUE(isRetryableHttpStatus(500));  // Internal server error (booting)
  EXPECT_TRUE(isRetryableHttpStatus(502));  // Bad Gateway (proxy up, server rebooting)
  EXPECT_TRUE(isRetryableHttpStatus(503));  // Service Unavailable (server restarting)
  EXPECT_TRUE(isRetryableHttpStatus(504));  // Gateway Timeout
}

TEST(OpdsRetryPolicy, TransientNetworkErrorsAreRetryable) {
  EXPECT_TRUE(isRetryableHttpStatus(408));  // Request Timeout
  EXPECT_TRUE(isRetryableHttpStatus(429));  // Rate limit
}

TEST(OpdsRetryPolicy, AuthenticationFailureIsNeverRetryable) {
  // Bad credentials should prompt the user, not spam retries
  EXPECT_FALSE(isRetryableHttpStatus(401));
}

TEST(OpdsRetryPolicy, PermanentClientErrorsAreNeverRetryable) {
  EXPECT_FALSE(isRetryableHttpStatus(403));  // Forbidden
  EXPECT_FALSE(isRetryableHttpStatus(404));  // Not Found
  EXPECT_FALSE(isRetryableHttpStatus(400));  // Bad Request
}

TEST(OpdsRetryPolicy, SuccessIsNeverRetryable) {
  EXPECT_FALSE(isRetryableHttpStatus(200));
  EXPECT_FALSE(isRetryableHttpStatus(206));
}

TEST(OpdsRetryPolicy, RetriesOnTemporaryServerFailureUnderMaxAttempts) {
  EXPECT_TRUE(shouldRetry(0, 1));
  EXPECT_TRUE(shouldRetry(0, 2));
  EXPECT_TRUE(shouldRetry(503, 1));
  EXPECT_TRUE(shouldRetry(502, 2));
}

TEST(OpdsRetryPolicy, AttemptsAreCappedAtMaxAttempts) {
  EXPECT_FALSE(shouldRetry(0, MAX_ATTEMPTS));
  EXPECT_FALSE(shouldRetry(0, MAX_ATTEMPTS + 1));
  EXPECT_FALSE(shouldRetry(503, MAX_ATTEMPTS));
}

TEST(OpdsRetryPolicy, BadCredentialsNeverRetriedEvenOnFirstAttempt) { EXPECT_FALSE(shouldRetry(401, 1)); }

TEST(OpdsRetryPolicy, BackoffGrowsExponentially) {
  EXPECT_EQ(backoffMs(1), 1000UL);
  EXPECT_EQ(backoffMs(2), 2000UL);
  EXPECT_EQ(backoffMs(3), 4000UL);
  EXPECT_EQ(backoffMs(4), 8000UL);
}

TEST(OpdsRetryPolicy, BackoffIsCappedAtMaximum) {
  EXPECT_EQ(backoffMs(5), MAX_BACKOFF_MS);
  EXPECT_EQ(backoffMs(10), MAX_BACKOFF_MS);
  EXPECT_EQ(backoffMs(100), MAX_BACKOFF_MS);
}

TEST(OpdsRetryPolicy, BackoffHandlesZeroAndNegativeAttemptsSafely) {
  EXPECT_EQ(backoffMs(0), 0UL);
  EXPECT_EQ(backoffMs(-1), 0UL);
}

TEST(OpdsRetryPolicy, TimeoutIsConfiguredForPromptReconnection) {
  // OPDS timeout should be responsive (15s) rather than blocking for 60s
  EXPECT_EQ(DEFAULT_TIMEOUT_MS, 15000U);
  EXPECT_LT(DEFAULT_TIMEOUT_MS, 60000U);
}
