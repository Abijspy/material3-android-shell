#pragma once

// The cryptography Quick Share's UKEY2 handshake and secure messages need,
// over OpenSSL's libcrypto: P-256 ECDH, SHA-256/512, HMAC-SHA256,
// HKDF-SHA256 and AES-256-CBC (PKCS#7 padded); for the QR code, ECDSA and
// AES-GCM.
#include <memory>
#include <string>

typedef struct evp_pkey_st EVP_PKEY;

namespace m3::quickshare::crypto {

using Bytes = std::string;

Bytes random(size_t n);
Bytes sha256(const Bytes& data);
Bytes sha512(const Bytes& data);
Bytes hmacSha256(const Bytes& key, const Bytes& data);
Bytes hkdfSha256(const Bytes& ikm, const Bytes& salt, const Bytes& info, size_t length);
// Empty on failure (a bad key, bad padding)
Bytes aesCbcEncrypt(const Bytes& key, const Bytes& iv, const Bytes& plain);
Bytes aesCbcDecrypt(const Bytes& key, const Bytes& iv, const Bytes& cipher);
// AES-128-GCM: iv (12) + ciphertext + tag (16) in one; empty on failure
Bytes aesGcmDecrypt(const Bytes& key, const Bytes& combined, const Bytes& aad);
// Constant time
bool equal(const Bytes& a, const Bytes& b);

// A P-256 key pair; its public point as 32-byte big-endian X and Y
class EcKey {
public:
    EcKey(); // a fresh pair
    ~EcKey();
    EcKey(const EcKey&) = delete;
    EcKey& operator=(const EcKey&) = delete;
    bool valid() const { return m_key != nullptr; }
    Bytes x() const { return m_x; }
    Bytes y() const { return m_y; }
    // The ECDH shared secret (the shared point's X, 32 bytes) with a peer's
    // public point; empty if the point isn't on the curve
    Bytes agree(const Bytes& peerX, const Bytes& peerY) const;
    // ECDSA with SHA-256: r and s, 32 bytes each (IEEE P1363)
    Bytes sign(const Bytes& message) const;

private:
    EVP_PKEY* m_key = nullptr;
    Bytes m_x, m_y;
};

} // namespace m3::quickshare::crypto
