#pragma once

#include <cstddef>
#include <cstdint>

// lector's release signature (scripts/sign_firmware.py). A signed image carries one
// block straight after its SHA-256 trailer: MAGIC, then the raw ECDSA P-256 signature
// r || s over that trailer. The bootloader never reads past the trailer, so the block
// costs nothing at boot; firmware_flash::validateImageStream checks it.
namespace firmware_signature {

inline constexpr uint8_t MAGIC[4] = {'L', 'S', 'G', '1'};
inline constexpr size_t SIG_SIZE = 64;
inline constexpr size_t BLOCK_SIZE = sizeof(MAGIC) + SIG_SIZE;
inline constexpr size_t PUBLIC_KEY_SIZE = 65;  // uncompressed SEC1 point: 0x04 || X || Y

// True when `sig` is `publicKey`'s signature over the 32-byte `digest`.
bool verify(const uint8_t publicKey[PUBLIC_KEY_SIZE], const uint8_t digest[32], const uint8_t sig[SIG_SIZE]);

// verify() against the release key built into this firmware.
bool verifyRelease(const uint8_t digest[32], const uint8_t sig[SIG_SIZE]);

}  // namespace firmware_signature
