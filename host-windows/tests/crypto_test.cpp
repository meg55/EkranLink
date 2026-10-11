// ChaCha20 / Poly1305 / AEAD doğrulaması: RFC 8439 test vektörleri + Android tarafıyla çapraz kontrol.
// Çalıştır: build\ekranlink-test.exe  (tümü GEÇTİ olmalı)

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/chacha.h"
#include "../src/crypto.h"

using namespace ekran;

namespace {

int failures = 0;

std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    int hi = -1;
    for (; *s; s++) {
        int v = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : -1;
        if (v < 0) continue;
        if (hi < 0) hi = v;
        else { out.push_back(uint8_t(hi * 16 + v)); hi = -1; }
    }
    return out;
}

std::string tohex(const uint8_t* p, size_t n) {
    std::string s;
    char b[3];
    for (size_t i = 0; i < n; i++) { std::snprintf(b, 3, "%02x", p[i]); s += b; }
    return s;
}

void expect(const char* name, const uint8_t* got, const std::vector<uint8_t>& want) {
    const bool ok = std::memcmp(got, want.data(), want.size()) == 0;
    std::printf("%-34s %s\n", name, ok ? "GEÇTİ" : "KALDI");
    if (!ok) {
        std::printf("   beklenen %s\n   bulunan  %s\n", tohex(want.data(), want.size()).c_str(), tohex(got, want.size()).c_str());
        failures++;
    }
}

const char* kSunscreen =
    "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";

}  // namespace

int main() {
    // RFC 8439 2.4.2 — ChaCha20
    {
        auto key = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
        auto nonce = hex("000000000000004a00000000");
        std::vector<uint8_t> m(kSunscreen, kSunscreen + std::strlen(kSunscreen));
        chacha::xor20(key.data(), nonce.data(), 1, m.data(), m.size());
        expect("RFC 8439 2.4.2 ChaCha20", m.data(),
               hex("6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b357"
                   "1639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
                   "5af90bbf74a35be6b40b8eedf2785e42874d"));
    }
    // RFC 8439 2.5.2 — Poly1305
    {
        auto key = hex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
        const char* msg = "Cryptographic Forum Research Group";
        uint8_t tag[16];
        chacha::poly1305(key.data(), reinterpret_cast<const uint8_t*>(msg), std::strlen(msg), tag);
        expect("RFC 8439 2.5.2 Poly1305", tag, hex("a8061dc1305136c6c22b8baf0c0127a9"));
    }
    // RFC 8439 A.3 #5 ve #6 — Poly1305 taşma kenar durumları
    {
        uint8_t tag[16];
        auto k5 = hex("0200000000000000000000000000000000000000000000000000000000000000");
        auto m5 = hex("ffffffffffffffffffffffffffffffff");
        chacha::poly1305(k5.data(), m5.data(), m5.size(), tag);
        expect("RFC 8439 A.3 #5 Poly1305", tag, hex("03000000000000000000000000000000"));
        auto k6 = hex("02000000000000000000000000000000ffffffffffffffffffffffffffffffff");
        auto m6 = hex("02000000000000000000000000000000");
        chacha::poly1305(k6.data(), m6.data(), m6.size(), tag);
        expect("RFC 8439 A.3 #6 Poly1305", tag, hex("03000000000000000000000000000000"));
    }
    // RFC 8439 2.8.2 — AEAD (ek veriyle)
    {
        auto key = hex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
        auto nonce = hex("070000004041424344454647");
        auto aad = hex("50515253c0c1c2c3c4c5c6c7");
        std::vector<uint8_t> m(kSunscreen, kSunscreen + std::strlen(kSunscreen));
        uint8_t tag[16];
        chacha::seal(key.data(), nonce.data(), aad.data(), aad.size(), m.data(), m.size(), tag);
        expect("RFC 8439 2.8.2 AEAD şifreli metin", m.data(),
               hex("d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b"
                   "1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
                   "3ff4def08e4b7a9de576d26586cec64b6116"));
        expect("RFC 8439 2.8.2 AEAD etiket", tag, hex("1ae10b594f09e26a7e902ecbd0600691"));
        const bool opened = chacha::open(key.data(), nonce.data(), aad.data(), aad.size(), m.data(), m.size(), tag);
        std::printf("%-34s %s\n", "AEAD çözme", opened && std::memcmp(m.data(), kSunscreen, m.size()) == 0 ? "GEÇTİ" : "KALDI");
        if (!opened) failures++;
        tag[0] ^= 1;
        const bool forged = chacha::open(key.data(), nonce.data(), aad.data(), aad.size(), m.data(), m.size(), tag);
        std::printf("%-34s %s\n", "Bozuk etiket reddi", !forged ? "GEÇTİ" : "KALDI");
        if (forged) failures++;
    }
    // Protokol kullanımı (DirectionKey): Android'deki Crypto.DirectionKey ile aynı sonucu vermeli
    {
        crypto::Bytes32 key;
        for (int i = 0; i < 32; i++) key[size_t(i)] = uint8_t(i);
        crypto::DirectionKey dk;
        dk.init(key);
        uint8_t msg[] = "EkranLink capraz test";
        uint8_t tag[16];
        dk.encrypt(5, msg, sizeof msg - 1, tag);
        std::printf("Çapraz kontrol (seq=5): %s %s\n", tohex(msg, sizeof msg - 1).c_str(), tohex(tag, 16).c_str());
    }

    std::printf(failures ? "\n%d test KALDI\n" : "\nTüm testler geçti\n", failures);
    return failures ? 1 : 0;
}
