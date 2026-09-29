#include "chacha.h"

#include <cstring>

namespace ekran::chacha {

namespace {

inline uint32_t rotl(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }

inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline void put32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

#define QR(a, b, c, d)                  \
    a += b; d = rotl(d ^ a, 16);        \
    c += d; b = rotl(b ^ c, 12);        \
    a += b; d = rotl(d ^ a, 8);         \
    c += d; b = rotl(b ^ c, 7)

void block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    std::memcpy(x, in, sizeof x);
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) put32(out + 4 * i, x[i] + in[i]);
}

#undef QR

// poly1305-donna (26 bitlik parçalar)
class Poly {
public:
    explicit Poly(const uint8_t key[32]) {
        r_[0] = le32(key) & 0x3ffffff;
        r_[1] = (le32(key + 3) >> 2) & 0x3ffff03;
        r_[2] = (le32(key + 6) >> 4) & 0x3ffc0ff;
        r_[3] = (le32(key + 9) >> 6) & 0x3f03fff;
        r_[4] = (le32(key + 12) >> 8) & 0x00fffff;
        for (int i = 0; i < 4; i++) pad_[i] = le32(key + 16 + 4 * i);
    }

    void update(const uint8_t* m, size_t n) {
        if (partialLen_) {
            size_t take = 16 - partialLen_ < n ? 16 - partialLen_ : n;
            std::memcpy(partial_ + partialLen_, m, take);
            partialLen_ += take;
            m += take;
            n -= take;
            if (partialLen_ < 16) return;
            blocks(partial_, 16, 1u << 24);
            partialLen_ = 0;
        }
        size_t full = n & ~size_t(15);
        if (full) blocks(m, full, 1u << 24);
        m += full;
        n -= full;
        if (n) {
            std::memcpy(partial_, m, n);
            partialLen_ = n;
        }
    }

    void padTo16() {
        if (!partialLen_) return;
        std::memset(partial_ + partialLen_, 0, 16 - partialLen_);
        blocks(partial_, 16, 1u << 24);
        partialLen_ = 0;
    }

    void finish(uint8_t tag[16]) {
        if (partialLen_) {
            partial_[partialLen_] = 1;
            std::memset(partial_ + partialLen_ + 1, 0, 15 - partialLen_);
            blocks(partial_, 16, 0);
        }
        uint32_t h0 = h_[0], h1 = h_[1], h2 = h_[2], h3 = h_[3], h4 = h_[4], c;
        c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
        c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
        c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
        c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
        c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

        uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
        uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
        uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
        uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
        uint32_t g4 = h4 + c - (1u << 26);

        uint32_t mask = (g4 >> 31) - 1;  // h >= p ise g'yi seç
        g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
        mask = ~mask;
        h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
        h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

        h0 = h0 | (h1 << 26);
        h1 = (h1 >> 6) | (h2 << 20);
        h2 = (h2 >> 12) | (h3 << 14);
        h3 = (h3 >> 18) | (h4 << 8);

        uint64_t f = uint64_t(h0) + pad_[0]; put32(tag, uint32_t(f));
        f = uint64_t(h1) + pad_[1] + (f >> 32); put32(tag + 4, uint32_t(f));
        f = uint64_t(h2) + pad_[2] + (f >> 32); put32(tag + 8, uint32_t(f));
        f = uint64_t(h3) + pad_[3] + (f >> 32); put32(tag + 12, uint32_t(f));
    }

private:
    void blocks(const uint8_t* m, size_t n, uint32_t hibit) {
        const uint32_t r0 = r_[0], r1 = r_[1], r2 = r_[2], r3 = r_[3], r4 = r_[4];
        const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
        uint32_t h0 = h_[0], h1 = h_[1], h2 = h_[2], h3 = h_[3], h4 = h_[4];
        for (; n >= 16; m += 16, n -= 16) {
            h0 += le32(m) & 0x3ffffff;
            h1 += (le32(m + 3) >> 2) & 0x3ffffff;
            h2 += (le32(m + 6) >> 4) & 0x3ffffff;
            h3 += (le32(m + 9) >> 6) & 0x3ffffff;
            h4 += (le32(m + 12) >> 8) | hibit;

            uint64_t d0 = uint64_t(h0) * r0 + uint64_t(h1) * s4 + uint64_t(h2) * s3 + uint64_t(h3) * s2 + uint64_t(h4) * s1;
            uint64_t d1 = uint64_t(h0) * r1 + uint64_t(h1) * r0 + uint64_t(h2) * s4 + uint64_t(h3) * s3 + uint64_t(h4) * s2;
            uint64_t d2 = uint64_t(h0) * r2 + uint64_t(h1) * r1 + uint64_t(h2) * r0 + uint64_t(h3) * s4 + uint64_t(h4) * s3;
            uint64_t d3 = uint64_t(h0) * r3 + uint64_t(h1) * r2 + uint64_t(h2) * r1 + uint64_t(h3) * r0 + uint64_t(h4) * s4;
            uint64_t d4 = uint64_t(h0) * r4 + uint64_t(h1) * r3 + uint64_t(h2) * r2 + uint64_t(h3) * r1 + uint64_t(h4) * r0;

            uint32_t c = uint32_t(d0 >> 26); h0 = uint32_t(d0) & 0x3ffffff;
            d1 += c; c = uint32_t(d1 >> 26); h1 = uint32_t(d1) & 0x3ffffff;
            d2 += c; c = uint32_t(d2 >> 26); h2 = uint32_t(d2) & 0x3ffffff;
            d3 += c; c = uint32_t(d3 >> 26); h3 = uint32_t(d3) & 0x3ffffff;
            d4 += c; c = uint32_t(d4 >> 26); h4 = uint32_t(d4) & 0x3ffffff;
            h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
            h1 += c;
        }
        h_[0] = h0; h_[1] = h1; h_[2] = h2; h_[3] = h3; h_[4] = h4;
    }

    uint32_t r_[5], h_[5] = {0, 0, 0, 0, 0}, pad_[4];
    uint8_t partial_[16];
    size_t partialLen_ = 0;
};

void aeadTag(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, const uint8_t* ct,
             size_t n, uint8_t tag[16]) {
    uint8_t otk[64] = {};
    xor20(key, nonce, 0, otk, 64);  // blok 0'ın ilk 32 baytı Poly1305 anahtarı
    Poly p(otk);
    p.update(aad, aadLen);
    p.padTo16();
    p.update(ct, n);
    p.padTo16();
    uint8_t lens[16];
    for (int i = 0; i < 8; i++) {
        lens[i] = uint8_t(uint64_t(aadLen) >> (8 * i));
        lens[8 + i] = uint8_t(uint64_t(n) >> (8 * i));
    }
    p.update(lens, 16);
    p.finish(tag);
}

}  // namespace

void xor20(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t* b, size_t n) {
    uint32_t st[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; i++) st[4 + i] = le32(key + 4 * i);
    st[12] = counter;
    st[13] = le32(nonce);
    st[14] = le32(nonce + 4);
    st[15] = le32(nonce + 8);
    uint8_t ks[64];
    while (n > 0) {
        block(st, ks);
        st[12]++;
        const size_t take = n < 64 ? n : 64;
        for (size_t i = 0; i < take; i++) b[i] ^= ks[i];
        b += take;
        n -= take;
    }
}

void poly1305(const uint8_t key[32], const uint8_t* m, size_t n, uint8_t tag[16]) {
    Poly p(key);
    p.update(m, n);
    p.finish(tag);
}

void seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, uint8_t* data,
          size_t n, uint8_t tag[16]) {
    xor20(key, nonce, 1, data, n);
    aeadTag(key, nonce, aad, aadLen, data, n, tag);
}

bool open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, uint8_t* data, size_t n,
          const uint8_t tag[16]) {
    uint8_t expect[16];
    aeadTag(key, nonce, aad, aadLen, data, n, expect);
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= uint8_t(expect[i] ^ tag[i]);  // sabit süreli karşılaştırma
    if (diff) return false;
    xor20(key, nonce, 1, data, n);
    return true;
}

}  // namespace ekran::chacha
