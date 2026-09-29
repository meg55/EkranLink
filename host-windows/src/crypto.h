// Windows CNG (bcrypt) üzerinde küçük şifreleme yardımcıları. Dış kütüphane yok.
// Android 4.x ile ortak payda: ECDH P-256 ve SHA-256/HMAC (anahtar türetme). Veri şifrelemesi: chacha.h
#pragma once

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ekran::crypto {

using Bytes32 = std::array<uint8_t, 32>;

bool random(void* out, size_t n);
Bytes32 sha256(const uint8_t* data, size_t n);
Bytes32 hmac(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t n);
// HMAC(key, label) — anahtar türetme için
Bytes32 derive(const Bytes32& prk, const char* label);

class Ecdh {
public:
    ~Ecdh();
    bool generate();
    std::array<uint8_t, 65> publicKey() const { return pub_; }  // 0x04 || X || Y
    bool agree(const uint8_t* peerPub65, Bytes32& secret) const;

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
    std::array<uint8_t, 65> pub_{};
};

// Tek yön: ChaCha20-Poly1305 AEAD (RFC 8439, ek veri boş). Nonce = 4 bayt sıfır || sıra no (LE64).
// İki yönün anahtarı farklı; sıra no her mesajda artar, nonce hiç tekrarlanmaz.
class DirectionKey {
public:
    ~DirectionKey() { SecureZeroMemory(key_, sizeof key_); }
    bool init(const Bytes32& key);
    void encrypt(uint64_t seq, uint8_t* data, size_t n, uint8_t tag[16]);
    bool decrypt(uint64_t seq, uint8_t* data, size_t n, const uint8_t tag[16]);

private:
    uint8_t key_[32] = {};
};
}  // namespace ekran::crypto
