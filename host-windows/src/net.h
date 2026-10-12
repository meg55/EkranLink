// Tablete TCP bağlantısı, çerçeveleme ve şifreleme. Gönderim iş parçacığı güvenli.
// Cihaz→PC her zaman şifreli ve doğrulanmış; PC→cihaz görüntüsü yalnızca USB'de (yerel bağlantı) şifresiz.
#pragma once

#include <winsock2.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "crypto.h"
#include "protocol.h"

namespace ekran {

struct PairingHooks {
    // İlk eşleştirme (USB ve Wi-Fi): cihazda da görünen kodu kullanıcıya onaylat (engelleyici çağrı).
    // cancel: tablet reddedip bağlantıyı kapatırsa tetiklenir; soru penceresi kapanmalı.
    std::function<bool(const std::string& deviceName, const std::string& code, HANDLE cancel)> confirm;
    // Eşleşmemiş cihaz için soru açılabilir mi: kullanıcı "Bağlan" dedi ya da cihaz USB'ye yeni takıldı.
    // Değilse (ör. ağdan kendiliğinden bulunan cihaz) soru açılmaz; needsPairing() true döner.
    bool mayPair = false;
    // Kayıtlı anahtar önceki bağlantıda doğrulanamadı: yok say, yeniden eşleştir (mayPair gerekir)
    bool ignoreKey = false;
};

class Connection {
public:
    ~Connection() { close(); }

    bool connect(const std::string& host, uint16_t port, int timeoutMs);
    // Anahtar değişimi + eşleştirme + tabletin Hello mesajı. localLink: USB kablosu (adb forward); görüntü
    // şifresiz gidebilir. Kimlik doğrulaması her bağlantıda aynıdır: eşleşme anahtarı ya da onaylanan kod.
    bool handshake(bool localLink, const PairingHooks& hooks, proto::Hello& hello);

    bool send(uint8_t type, const std::vector<uint8_t>& payload);
    bool sendVideo(int64_t ptsUs, uint8_t flags, const uint8_t* data, size_t size);
    // Okuma iş parçacığından çağrılır; bağlantı koparsa veya doğrulama başarısızsa false.
    bool readMessage(uint8_t& type, std::vector<uint8_t>& payload);

    void close();
    bool alive() const { return sock_ != INVALID_SOCKET && !broken_; }
    bool encrypted() const { return tx_ != nullptr; }
    const std::string& deviceId() const { return deviceId_; }
    bool pairingRejected() const { return pairingRejected_; }
    // Cihaz eşleşmemiş (ya da eşleşmeyi unutmuş) ve soru açmaya izin yoktu: kullanıcı "Bağlan" demeli
    bool needsPairing() const { return needsPairing_; }
    // Eşleşmiş sanılan cihaz doğrulanamadı (anahtarlar uyuşmuyor ya da bağlantı el sıkışmada koptu)
    bool authFailed() const { return authFailed_; }
    // El sıkışmayı tablet sürüm uyuşmazlığıyla reddettiyse tabletin protokol ve uygulama sürümü; yoksa 0
    uint16_t peerVersion() const { return peerVersion_; }
    const std::string& peerAppVersion() const { return peerAppVersion_; }

private:
    // Gövde = tür + yük. Şifreli bağlantıda gövde şifrelenir ve 16 bayt etiket eklenir.
    bool sendFrame(uint8_t type, const uint8_t* head, size_t headLen, const uint8_t* data, size_t dataLen);
    bool sendAll(WSABUF* bufs, DWORD count);
    bool recvAll(void* p, size_t n);

    SOCKET sock_ = INVALID_SOCKET;
    std::mutex sendMutex_;
    volatile bool broken_ = false;
    std::unique_ptr<crypto::DirectionKey> tx_, rx_;
    uint64_t txSeq_ = 0, rxSeq_ = 0;
    std::vector<uint8_t> txBuf_;
    std::string deviceId_;
    bool pairingRejected_ = false;
    bool needsPairing_ = false;
    bool authFailed_ = false;
    uint16_t peerVersion_ = 0;
    std::string peerAppVersion_;
};

}  // namespace ekran
