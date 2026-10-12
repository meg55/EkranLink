// EkranLink protokol v3 sabitleri. Ayrıntı: docs/PROTOCOL.md
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ekran::proto {

constexpr uint16_t kPort = 27183;
constexpr uint16_t kVersion = 3;
constexpr char kMagic[4] = {'E', 'K', 'R', 'N'};
// Kimlik doğrulanmadan önce kabul edilen en büyük çerçeve (bellek tüketme saldırısına karşı)
constexpr uint32_t kMaxHandshakeFrame = 4096;

enum Msg : uint8_t {
    kHello = 1,
    kConfig = 2,
    kVideo = 3,
    kPing = 4,
    kPong = 5,
    kFrameAck = 6,
    kCursorShape = 7,
    kCursorPos = 8,
    kTouch = 10,
    kOrientation = 11,  // T→P: tabletin tutuluş yönü (u16 derece)
    kDisplayPower = 12, // P→T: Windows ekranları açık mı (u8)
    kKeyRequest = 13,   // T→P: çözücü yeniden kuruldu, anahtar kare gönder
    kVideoVisible = 14, // T→P: tablet uygulaması önde mi (u8); arka plandayken görüntü üretilmez
    // Anahtar değişimi / eşleştirme (Mode'a kadar şifresiz, sonrası şifreli)
    kPcHello = 20,
    kTabletKey = 21,
    kMode = 22,
    kPairResult = 23,  // şifreli; eşleşmişte PC'nin anahtar onayı, ilk eşleştirmede kullanıcının kararı
    // T→P, kPcHello yerine: tablet PC'nin protokol sürümünü desteklemiyor (u16 tablet sürümü, str uygulama sürümü).
    // Ardından tablet bağlantıyı kapatır; PC kullanıcıya hangi tarafın güncelleneceğini söyler.
    kIncompatible = 24,
};

enum PcHelloFlags : uint8_t {
    kWantPlain = 1,  // USB: PC→cihaz görüntüsü şifresiz olsun (cihaz yalnızca yerel bağlantıda kabul eder)
};

// Cihazdan gelen ad: denetim karakterleri (sekme, satır sonu, NUL) atılır, en çok 64 bayt (UTF-8 harfi bölmeden).
// Ad soruda, günlükte ve kayıt defterindeki cihaz listesinde (sekmeyle ayrılmış) görünür.
inline std::string cleanName(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c >= 0x20 && c != 0x7F) out += char(c);
    if (out.size() > 64) {
        size_t n = 64;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) n--;
        out.resize(n);
    }
    return out;
}

// Cihaz kimliği: Android kimliği ya da seri no; yalnızca harf, rakam, '-' ve '_' (kayıt defteri adı olur)
inline bool validId(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (unsigned char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_'))
            return false;
    return true;
}

enum VideoFlags : uint8_t {
    kKeyFrame = 1,
    kPusher = 2,
};

enum TouchAction : uint8_t {
    kTouchDown = 0,
    kTouchMove = 1,
    kTouchUp = 2,
    kTouchCancel = 3,
};

struct Hello {
    uint16_t version = 0;
    uint16_t surfaceW = 0, surfaceH = 0;
    uint16_t screenW = 0, screenH = 0;
    uint16_t dpi = 0, sdk = 0;
    std::string model, decoder;
};

// Big-endian yazıcı
class Writer {
public:
    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v >> 8)); u8(uint8_t(v)); }
    void u32(uint32_t v) { u16(uint16_t(v >> 16)); u16(uint16_t(v)); }
    void u64(uint64_t v) { u32(uint32_t(v >> 32)); u32(uint32_t(v)); }
    void bytes(const void* p, size_t n) {
        auto b = static_cast<const uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    std::vector<uint8_t> buf;
};

// Big-endian okuyucu (sınır denetimli)
class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    uint8_t u8() { return need(1) ? p_[pos_++] : 0; }
    uint16_t u16() { uint16_t hi = u8(); return uint16_t((hi << 8) | u8()); }
    uint32_t u32() { uint32_t hi = u16(); return (hi << 16) | u16(); }
    uint64_t u64() { uint64_t hi = u32(); return (hi << 32) | u32(); }
    std::string str() {
        uint16_t n = u16();
        if (!need(n)) return {};
        std::string s(reinterpret_cast<const char*>(p_ + pos_), n);
        pos_ += n;
        return s;
    }

private:
    bool need(size_t n) {
        if (!ok_ || pos_ + n > n_) { ok_ = false; return false; }
        return true;
    }
    const uint8_t* p_;
    size_t n_, pos_ = 0;
    bool ok_ = true;
};

}  // namespace ekran::proto
