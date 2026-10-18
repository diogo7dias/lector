#include <gtest/gtest.h>
#include <wolfssl/wolfcrypt/asn_public.h>

#include <cstring>
#include <deque>
#include <string>

#include "CertPinVerify.h"

// Real TLS handshakes, client and server in one process over in-memory pipes, so the
// pin callback is checked against wolfSSL's own verification and host check rather
// than a stand-in. Fixtures: a test CA, a leaf it signed for books.example, and a
// self-signed leaf for home.local (openssl, P-256, valid until 2125).
namespace {

constexpr char CA_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
MIIBjzCCATWgAwIBAgIUDDJ7apgy3IYW4qJ7VC6mOSYXezEwCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJVGVzdCBSb290MCAXDTI2MTAxODE5NDAwOFoYDzIxMjYwOTI0
MTk0MDA4WjAUMRIwEAYDVQQDDAlUZXN0IFJvb3QwWTATBgcqhkjOPQIBBggqhkjO
PQMBBwNCAAS7JyDX8kx00R/5tTlOSAJjUhtHEPNAh0wbk0QLhzFDwdVPanzUOsDV
qr4dq2eyo760l95QV6UxIjRMPFPzu4hDo2MwYTAdBgNVHQ4EFgQUyAKkCK9hLUVi
sSw7mReCiICyCPYwHwYDVR0jBBgwFoAUyAKkCK9hLUVisSw7mReCiICyCPYwDwYD
VR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAgQwCgYIKoZIzj0EAwIDSAAwRQIg
MEVtttgh8hqeaFuDqFG5C60gXO4kP2LnGhEHcMexypUCIQDDU1RUchiePxc5edNB
6NFgGKov0a893JUl4gAqxl+mGQ==
-----END CERTIFICATE-----
)PEM";

constexpr char LEAF_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
MIIBlzCCAT2gAwIBAgIUaozD6672ZOYXipLyLIsQX7fpLKgwCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJVGVzdCBSb290MCAXDTI2MTAxODE5NDAwOFoYDzIxMjYwOTI0
MTk0MDA4WjAYMRYwFAYDVQQDDA1ib29rcy5leGFtcGxlMFkwEwYHKoZIzj0CAQYI
KoZIzj0DAQcDQgAE9G6qvKCGO4kzhQv8N4uhy2LFkE1+iu9WcPt+hV69KW+lxXMa
IEby3qkNsmAd8inSt/WErZTSOjvKi977ozq9gqNnMGUwGAYDVR0RBBEwD4INYm9v
a3MuZXhhbXBsZTAJBgNVHRMEAjAAMB0GA1UdDgQWBBRWLqrYMY9gc0inRVJZSc9p
2QxvtjAfBgNVHSMEGDAWgBTIAqQIr2EtRWKxLDuZF4KIgLII9jAKBggqhkjOPQQD
AgNIADBFAiEAx9YReB/QWa+3O3ASgReMsyMfwdhcOshwJNd46iHpdKECIHx57KZI
r68pKkCVmdArRnuJ6CIXMG+tDLH8ssnZ3td4
-----END CERTIFICATE-----
)PEM";

constexpr char LEAF_KEY[] = R"PEM(
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIF7lMRFnLGdIbJrsYYPKYJWiUTbCe5j5zAjjCniQ9/vEoAoGCCqGSM49
AwEHoUQDQgAE9G6qvKCGO4kzhQv8N4uhy2LFkE1+iu9WcPt+hV69KW+lxXMaIEby
3qkNsmAd8inSt/WErZTSOjvKi977ozq9gg==
-----END EC PRIVATE KEY-----
)PEM";

constexpr char SELF_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
MIIBmTCCAT6gAwIBAgIUYCfLexHr8q1o5WKRbU6Bn9KtLQowCgYIKoZIzj0EAwIw
FTETMBEGA1UEAwwKaG9tZS5sb2NhbDAgFw0yNjEwMTgxOTQwMDhaGA8yMTI2MDky
NDE5NDAwOFowFTETMBEGA1UEAwwKaG9tZS5sb2NhbDBZMBMGByqGSM49AgEGCCqG
SM49AwEHA0IABLA6YRfvgJO7wX8dGfcKllBgld/S0z2i/6rk0EoaiR7yoZ0ndYSL
0zRI0laybvj44OZZ/fhQKhzzx42BSqnIpKyjajBoMB0GA1UdDgQWBBRBZKiSt5Zb
Yxpz83dbn1mGLh0AmTAfBgNVHSMEGDAWgBRBZKiSt5ZbYxpz83dbn1mGLh0AmTAP
BgNVHRMBAf8EBTADAQH/MBUGA1UdEQQOMAyCCmhvbWUubG9jYWwwCgYIKoZIzj0E
AwIDSQAwRgIhAMKjG0LWXeGn7I8RCkDIdJp5aVOiUrz+oxMfjgqsZwdFAiEAnZue
B6D1J4JDVddZT7BwB0K/rjUrZUqMOVbbkKAvePI=
-----END CERTIFICATE-----
)PEM";

constexpr char SELF_KEY[] = R"PEM(
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIBP9QN6SKs6+K1ZL01tD9PZkRKz4r1CPSHdX09gI+/76oAoGCCqGSM49
AwEHoUQDQgAEsDphF++Ak7vBfx0Z9wqWUGCV39LTPaL/quTQShqJHvKhnSd1hIvT
NEjSVrJu+Pjg5ln9+FAqHPPHjYFKqcikrA==
-----END EC PRIVATE KEY-----
)PEM";

using freeink::cert_pin::SHA256_SIZE;
using freeink::cert_pin::State;

struct Pipe {
  std::deque<unsigned char> toClient, toServer;
};

int recvFrom(std::deque<unsigned char>& q, char* buf, int sz) {
  if (q.empty()) return WOLFSSL_CBIO_ERR_WANT_READ;
  int n = 0;
  for (; n < sz && !q.empty(); ++n) {
    buf[n] = static_cast<char>(q.front());
    q.pop_front();
  }
  return n;
}
int clientRecv(WOLFSSL*, char* buf, int sz, void* ctx) { return recvFrom(static_cast<Pipe*>(ctx)->toClient, buf, sz); }
int serverRecv(WOLFSSL*, char* buf, int sz, void* ctx) { return recvFrom(static_cast<Pipe*>(ctx)->toServer, buf, sz); }
int clientSend(WOLFSSL*, char* buf, int sz, void* ctx) {
  static_cast<Pipe*>(ctx)->toServer.insert(static_cast<Pipe*>(ctx)->toServer.end(), buf, buf + sz);
  return sz;
}
int serverSend(WOLFSSL*, char* buf, int sz, void* ctx) {
  static_cast<Pipe*>(ctx)->toClient.insert(static_cast<Pipe*>(ctx)->toClient.end(), buf, buf + sz);
  return sz;
}

void leafSha(const char* pem, uint8_t out[SHA256_SIZE]) {
  unsigned char der[2048];
  const int len = wc_CertPemToDer(reinterpret_cast<const unsigned char*>(pem), static_cast<int>(strlen(pem)), der,
                                  sizeof(der), CERT_TYPE);
  ASSERT_GT(len, 0);
  ASSERT_EQ(wc_Sha256Hash(der, static_cast<word32>(len), out), 0);
}

// Handshake with a server presenting certPem; the client trusts CA_PEM and expects `host`.
bool handshake(const char* certPem, const char* keyPem, const char* host, State& state) {
  Pipe pipe;
  WOLFSSL_CTX* sctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method());
  WOLFSSL_CTX* cctx = wolfSSL_CTX_new(wolfTLSv1_3_client_method());
  EXPECT_EQ(wolfSSL_CTX_use_certificate_buffer(sctx, reinterpret_cast<const unsigned char*>(certPem),
                                               static_cast<long>(strlen(certPem)), WOLFSSL_FILETYPE_PEM),
            WOLFSSL_SUCCESS);
  EXPECT_EQ(wolfSSL_CTX_use_PrivateKey_buffer(sctx, reinterpret_cast<const unsigned char*>(keyPem),
                                              static_cast<long>(strlen(keyPem)), WOLFSSL_FILETYPE_PEM),
            WOLFSSL_SUCCESS);
  EXPECT_EQ(wolfSSL_CTX_load_verify_buffer(cctx, reinterpret_cast<const unsigned char*>(CA_PEM),
                                           static_cast<long>(strlen(CA_PEM)), WOLFSSL_FILETYPE_PEM),
            WOLFSSL_SUCCESS);
  wolfSSL_CTX_SetIORecv(sctx, serverRecv);
  wolfSSL_CTX_SetIOSend(sctx, serverSend);
  wolfSSL_CTX_SetIORecv(cctx, clientRecv);
  wolfSSL_CTX_SetIOSend(cctx, clientSend);
  WOLFSSL* server = wolfSSL_new(sctx);
  WOLFSSL* client = wolfSSL_new(cctx);
  for (WOLFSSL* s : {server, client}) {
    wolfSSL_SetIOReadCtx(s, &pipe);
    wolfSSL_SetIOWriteCtx(s, &pipe);
  }
  freeink::cert_pin::arm(client, host, &state);

  bool clientDone = false, serverDone = false, failed = false;
  for (int round = 0; round < 50 && !failed && !(clientDone && serverDone); ++round) {
    if (!clientDone) {
      const int r = wolfSSL_connect(client);
      if (r == WOLFSSL_SUCCESS)
        clientDone = true;
      else if (wolfSSL_get_error(client, r) != WOLFSSL_ERROR_WANT_READ)
        failed = true;
    }
    if (!serverDone && !failed) {
      const int r = wolfSSL_accept(server);
      if (r == WOLFSSL_SUCCESS)
        serverDone = true;
      else if (wolfSSL_get_error(server, r) != WOLFSSL_ERROR_WANT_READ)
        failed = true;
    }
  }
  wolfSSL_free(client);
  wolfSSL_free(server);
  wolfSSL_CTX_free(cctx);
  wolfSSL_CTX_free(sctx);
  return clientDone && !failed;
}

class CertPin : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { wolfSSL_Init(); }
  static void TearDownTestSuite() { wolfSSL_Cleanup(); }
};

}  // namespace

TEST_F(CertPin, CaSignedLeafForTheRightHostConnects) {
  State state;
  EXPECT_TRUE(handshake(LEAF_PEM, LEAF_KEY, "books.example", state));
  EXPECT_FALSE(state.rejected);
}

// The host check: a valid certificate for another name is a man in the middle.
TEST_F(CertPin, CaSignedLeafForAnotherHostIsRejected) {
  State state;
  EXPECT_FALSE(handshake(LEAF_PEM, LEAF_KEY, "evil.example", state));
  EXPECT_TRUE(state.rejected);
}

TEST_F(CertPin, SelfSignedLeafIsRejectedAndShowsItsFingerprint) {
  State state;
  EXPECT_FALSE(handshake(SELF_PEM, SELF_KEY, "home.local", state));
  ASSERT_TRUE(state.rejected);
  uint8_t expected[SHA256_SIZE];
  leafSha(SELF_PEM, expected);
  EXPECT_EQ(std::memcmp(state.seen, expected, SHA256_SIZE), 0);
}

TEST_F(CertPin, PinnedSelfSignedLeafConnects) {
  State state;
  state.hasPin = true;
  leafSha(SELF_PEM, state.pin);
  EXPECT_TRUE(handshake(SELF_PEM, SELF_KEY, "home.local", state));
  EXPECT_FALSE(state.rejected);
}

TEST_F(CertPin, PinForAnotherCertificateDoesNotVouch) {
  State state;
  state.hasPin = true;
  leafSha(LEAF_PEM, state.pin);
  EXPECT_FALSE(handshake(SELF_PEM, SELF_KEY, "home.local", state));
  EXPECT_TRUE(state.rejected);
}

// A pin is for one exact certificate, so it vouches whatever name that certificate carries:
// home servers are often reached by IP while the cert names a hostname.
TEST_F(CertPin, PinVouchesForTheNameMismatchOfItsOwnCertificate) {
  State state;
  state.hasPin = true;
  leafSha(SELF_PEM, state.pin);
  EXPECT_TRUE(handshake(SELF_PEM, SELF_KEY, "192.168.1.20", state));
}
