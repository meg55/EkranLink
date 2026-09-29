#include "crypto.h"
#include "chacha.h"

#include <algorithm>
#include <cstring>

namespace ekran::crypto {

namespace {

}  // namespace

bool random(void* out, size_t n) {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

Bytes32 sha256(const uint8_t* data, size_t n) {
    Bytes32 out{};
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data), ULONG(n), out.data(), 32);
    return out;
}

Bytes32 hmac(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t n) {
    Bytes32 out{};
    BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, const_cast<PUCHAR>(key), ULONG(keyLen), const_cast<PUCHAR>(data),
               ULONG(n), out.data(), 32);
    return out;
}

Bytes32 derive(const Bytes32& prk, const char* label) {
    return hmac(prk.data(), prk.size(), reinterpret_cast<const uint8_t*>(label), std::strlen(label));
}

// --- ECDH P-256 ---

Ecdh::~Ecdh() {
    if (key_) BCryptDestroyKey(key_);
    if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
}

bool Ecdh::generate() {
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg_, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0))) return false;
    if (!BCRYPT_SUCCESS(BCryptGenerateKeyPair(alg_, &key_, 256, 0))) return false;
    if (!BCRYPT_SUCCESS(BCryptFinalizeKeyPair(key_, 0))) return false;
    uint8_t blob[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    ULONG n = 0;
    if (!BCRYPT_SUCCESS(BCryptExportKey(key_, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob, sizeof blob, &n, 0))) return false;
    pub_[0] = 0x04;  // sıkıştırılmamış nokta
    std::memcpy(pub_.data() + 1, blob + sizeof(BCRYPT_ECCKEY_BLOB), 64);
    return true;
}

bool Ecdh::agree(const uint8_t* peerPub65, Bytes32& secret) const {
    if (peerPub65[0] != 0x04) return false;
    uint8_t blob[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    auto* h = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob);
    h->dwMagic = BCRYPT_ECDH_PUBLIC_P256_MAGIC;
    h->cbKey = 32;
    std::memcpy(blob + sizeof(BCRYPT_ECCKEY_BLOB), peerPub65 + 1, 64);
    BCRYPT_KEY_HANDLE peer = nullptr;
    // İçe aktarma, noktanın eğri üzerinde olduğunu da doğrular.
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(alg_, nullptr, BCRYPT_ECCPUBLIC_BLOB, &peer, blob, sizeof blob, 0)))
        return false;
    BCRYPT_SECRET_HANDLE s = nullptr;
    bool ok = BCRYPT_SUCCESS(BCryptSecretAgreement(key_, peer, &s, 0));
    ULONG n = 0;
    if (ok) ok = BCRYPT_SUCCESS(BCryptDeriveKey(s, BCRYPT_KDF_RAW_SECRET, nullptr, secret.data(), 32, &n, 0)) && n == 32;
    // CNG ham sırrı küçük-endian verir; Java (ve standart) büyük-endian X koordinatı kullanır.
    if (ok) std::reverse(secret.begin(), secret.end());
    if (s) BCryptDestroySecret(s);
    BCryptDestroyKey(peer);
    return ok;
}

// --- ChaCha20-Poly1305 ---

bool DirectionKey::init(const Bytes32& key) {
    std::memcpy(key_, key.data(), 32);
    return true;
}

static void nonceFor(uint64_t seq, uint8_t nonce[12]) {
    std::memset(nonce, 0, 4);
    for (int i = 0; i < 8; i++) nonce[4 + i] = uint8_t(seq >> (8 * i));
}

void DirectionKey::encrypt(uint64_t seq, uint8_t* data, size_t n, uint8_t tag[16]) {
    uint8_t nonce[12];
    nonceFor(seq, nonce);
    chacha::seal(key_, nonce, nullptr, 0, data, n, tag);
}

bool DirectionKey::decrypt(uint64_t seq, uint8_t* data, size_t n, const uint8_t tag[16]) {
    uint8_t nonce[12];
    nonceFor(seq, nonce);
    return chacha::open(key_, nonce, nullptr, 0, data, n, tag);
}
}  // namespace ekran::crypto
