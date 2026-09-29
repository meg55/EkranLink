// ChaCha20-Poly1305 AEAD (RFC 8439). Android tarafıyla aynı yapı: android/.../Crypto.java
// Eski ARM tabletlerde AES'ten çok daha hızlı olduğu için seçildi (bkz. docs/ROADMAP.md).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ekran::chacha {

// b[0..n) üzerine ChaCha20 anahtar akışını XOR'lar. nonce 12 bayt, counter başlangıç blok sayacı.
void xor20(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t* b, size_t n);

void poly1305(const uint8_t key[32], const uint8_t* m, size_t n, uint8_t tag[16]);

// Yerinde şifrele + etiket üret / etiketi doğrula + yerinde çöz.
void seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, uint8_t* data,
          size_t n, uint8_t tag[16]);
bool open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, uint8_t* data,
          size_t n, const uint8_t tag[16]);

}  // namespace ekran::chacha
