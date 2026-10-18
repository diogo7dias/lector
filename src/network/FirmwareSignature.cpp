#include "FirmwareSignature.h"

#include <Memory.h>
#include <wolfssl/wolfcrypt/ecc.h>

namespace firmware_signature {

namespace {
// Public half of the release key. The private half is the FIRMWARE_SIGNING_KEY
// secret of the release workflow. Replacing it means every device needs one
// SD-card install of a firmware carrying the new key.
constexpr uint8_t RELEASE_KEY[PUBLIC_KEY_SIZE] = {
    0x04, 0xa7, 0x5e, 0x3f, 0xf6, 0xeb, 0x91, 0xda, 0x6a, 0x97, 0xd4, 0xe2, 0x44, 0x75, 0x2a, 0x02, 0x1e,
    0xb8, 0x3f, 0x56, 0xa4, 0x94, 0xf4, 0x7f, 0xfa, 0x4b, 0x0c, 0xb3, 0xb0, 0x9e, 0xef, 0x99, 0x37, 0xda,
    0x1d, 0x58, 0xd7, 0xa2, 0x42, 0x40, 0xef, 0xaf, 0x8e, 0x22, 0x93, 0x7d, 0x75, 0x90, 0xc4, 0x20, 0xdb,
    0x1f, 0x54, 0x8b, 0xec, 0x2d, 0xb6, 0x92, 0xae, 0xe6, 0x8a, 0xb7, 0x49, 0x74, 0x07};
}  // namespace

bool verify(const uint8_t publicKey[PUBLIC_KEY_SIZE], const uint8_t digest[32], const uint8_t sig[SIG_SIZE]) {
  if (publicKey[0] != 0x04) return false;
  // ecc_key holds the curve point and its big numbers: heap, not the caller's stack.
  // One allocation per firmware install.
  auto key = makeUniqueNoThrow<ecc_key>();
  if (!key || wc_ecc_init(key.get()) != 0) return false;

  uint8_t der[ECC_MAX_SIG_SIZE];
  word32 derLen = sizeof(der);
  int verified = 0;
  const bool ok = wc_ecc_import_unsigned(key.get(), publicKey + 1, publicKey + 33, nullptr, ECC_SECP256R1) == 0 &&
                  wc_ecc_rs_raw_to_sig(sig, SIG_SIZE / 2, sig + SIG_SIZE / 2, SIG_SIZE / 2, der, &derLen) == 0 &&
                  wc_ecc_verify_hash(der, derLen, digest, 32, &verified, key.get()) == 0 && verified == 1;
  wc_ecc_free(key.get());
  return ok;
}

bool verifyRelease(const uint8_t digest[32], const uint8_t sig[SIG_SIZE]) { return verify(RELEASE_KEY, digest, sig); }

}  // namespace firmware_signature
