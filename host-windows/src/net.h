// Tablete TCP bağlantısı, çerçeveleme ve (Wi-Fi'de) şifreleme. Gönderim iş parçacığı güvenli.
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
    // Wi-Fi'de ilk eşleştirme: tablette de görünen kodu kullanıcıya onaylat (engelleyici çağrı).
    // cancel: tablet reddedip bağlantıyı kapatırsa tetiklenir; soru penceresi kapanmalı.
    std::function<bool(const std::string& deviceName, const std::string& code, HANDLE cancel)> confirm;
};

class Connection {
public:
    ~Connection() { close(); }

    bool connect(const std::string& host, uint16_t port, int timeoutMs);
    // Anahtar değişimi + eşleştirme + tabletin Hello mesajı. trustedLink: USB (adb forward).
    bool handshake(bool trustedLink, const PairingHooks& hooks, proto::Hello& hello);

    bool send(uint8_t type, const std::vector<uint8_t>& payload);
    bool sendVideo(int64_t ptsUs, uint8_t flags, const uint8_t* data, size_t size);
    // Okuma iş parçacığından çağrılır; bağlantı koparsa veya doğrulama başarısızsa false.
    bool readMessage(uint8_t& type, std::vector<uint8_t>& payload);

    void close();
    bool alive() const { return sock_ != INVALID_SOCKET && !broken_; }
    bool encrypted() const { return tx_ != nullptr; }
    const std::string& deviceId() const { return deviceId_; }
    bool pairingRejected() const { return pairingRejected_; }

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
};

}  // namespace ekran
