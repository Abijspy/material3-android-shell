#include "quickshare/Crypto.h"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <vector>

namespace m3::quickshare::crypto {

namespace {
const unsigned char* u(const Bytes& b) {
    return reinterpret_cast<const unsigned char*>(b.data());
}
} // namespace

Bytes random(size_t n) {
    Bytes out(n, '\0');
    RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), static_cast<int>(n));
    return out;
}

Bytes sha256(const Bytes& data) {
    Bytes out(32, '\0');
    EVP_Digest(data.data(), data.size(), reinterpret_cast<unsigned char*>(out.data()), nullptr, EVP_sha256(), nullptr);
    return out;
}

Bytes sha512(const Bytes& data) {
    Bytes out(64, '\0');
    EVP_Digest(data.data(), data.size(), reinterpret_cast<unsigned char*>(out.data()), nullptr, EVP_sha512(), nullptr);
    return out;
}

Bytes hmacSha256(const Bytes& key, const Bytes& data) {
    Bytes out(32, '\0');
    unsigned int len = 32;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), u(data), data.size(), reinterpret_cast<unsigned char*>(out.data()), &len);
    return out;
}

Bytes hkdfSha256(const Bytes& ikm, const Bytes& salt, const Bytes& info, size_t length) {
    Bytes out(length, '\0');
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf) return {};
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    char digest[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, const_cast<char*>(ikm.data()), ikm.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char*>(salt.data()), salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, const_cast<char*>(info.data()), info.size()),
        OSSL_PARAM_construct_end(),
    };
    const bool ok = ctx && EVP_KDF_derive(ctx, reinterpret_cast<unsigned char*>(out.data()), length, params) > 0;
    EVP_KDF_CTX_free(ctx);
    return ok ? out : Bytes{};
}

namespace {
Bytes aesCbc(bool encrypt, const Bytes& key, const Bytes& iv, const Bytes& in) {
    if (key.size() != 32 || iv.size() != 16) return {};
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    Bytes out(in.size() + 16, '\0');
    int a = 0, b = 0;
    const bool ok = ctx && EVP_CipherInit_ex(ctx, EVP_aes_256_cbc(), nullptr, u(key), u(iv), encrypt ? 1 : 0) == 1 &&
                    EVP_CipherUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &a, u(in), static_cast<int>(in.size())) == 1 &&
                    EVP_CipherFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + a, &b) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};
    out.resize(static_cast<size_t>(a + b));
    return out;
}
} // namespace

Bytes aesCbcEncrypt(const Bytes& key, const Bytes& iv, const Bytes& plain) {
    return aesCbc(true, key, iv, plain);
}

Bytes aesCbcDecrypt(const Bytes& key, const Bytes& iv, const Bytes& cipher) {
    return aesCbc(false, key, iv, cipher);
}

bool equal(const Bytes& a, const Bytes& b) {
    return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

EcKey::EcKey() {
    m_key = EVP_EC_gen("P-256");
    if (!m_key) return;
    unsigned char point[65];
    size_t len = 0;
    if (EVP_PKEY_get_octet_string_param(m_key, OSSL_PKEY_PARAM_PUB_KEY, point, sizeof point, &len) != 1 || len != 65 || point[0] != 4) {
        EVP_PKEY_free(m_key);
        m_key = nullptr;
        return;
    }
    m_x.assign(reinterpret_cast<char*>(point + 1), 32);
    m_y.assign(reinterpret_cast<char*>(point + 33), 32);
}

EcKey::~EcKey() {
    EVP_PKEY_free(m_key);
}

Bytes EcKey::agree(const Bytes& peerX, const Bytes& peerY) const {
    if (!m_key || peerX.size() != 32 || peerY.size() != 32) return {};
    // The peer's point, uncompressed
    Bytes point = std::string(1, '\x04') + peerX + peerY;
    char group[] = "prime256v1";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()),
        OSSL_PARAM_construct_end(),
    };
    EVP_PKEY_CTX* fromCtx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
    EVP_PKEY* peer = nullptr;
    const bool built = fromCtx && EVP_PKEY_fromdata_init(fromCtx) == 1 && EVP_PKEY_fromdata(fromCtx, &peer, EVP_PKEY_PUBLIC_KEY, params) == 1;
    EVP_PKEY_CTX_free(fromCtx);
    if (!built) return {};
    // A point off the curve fails here
    EVP_PKEY_CTX* check = EVP_PKEY_CTX_new(peer, nullptr);
    const bool onCurve = check && EVP_PKEY_public_check(check) == 1;
    EVP_PKEY_CTX_free(check);
    Bytes secret;
    if (onCurve) {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(m_key, nullptr);
        size_t len = 0;
        if (ctx && EVP_PKEY_derive_init(ctx) == 1 && EVP_PKEY_derive_set_peer(ctx, peer) == 1 && EVP_PKEY_derive(ctx, nullptr, &len) == 1) {
            secret.assign(len, '\0');
            if (EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char*>(secret.data()), &len) != 1) secret.clear();
            else secret.resize(len);
        }
        EVP_PKEY_CTX_free(ctx);
    }
    EVP_PKEY_free(peer);
    return secret;
}

} // namespace m3::quickshare::crypto
