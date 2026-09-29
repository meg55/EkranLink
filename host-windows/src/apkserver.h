// Tablete APK'yı yerel ağdan sunan küçük HTTP sunucusu. Yalnızca istenince ve sınırlı süre açık kalır.
// Neden: eski tabletler GitHub'dan (TLS 1.2 / yeni kök sertifikalar) indirme yapamayabiliyor.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace ekran {

class ApkServer {
public:
    ~ApkServer() { stop(); }

    // apk yolunu port üzerinden `minutes` dakika sunar. Zaten açıksa süreyi uzatır.
    bool start(const std::wstring& apkPath, uint16_t port, int minutes);
    void stop();
    bool running() const { return running_; }
    // Tablette açılacak adresler (her ağ arayüzü için bir tane)
    std::vector<std::string> urls() const;

    static constexpr uint16_t kPort = 27184;

private:
    void loop();
    void serve(uintptr_t client);

    std::wstring apk_;
    uint16_t port_ = kPort;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> deadlineUs_{0};
    uintptr_t listen_ = ~uintptr_t(0);
    std::thread thread_;
};

// Bu PC'nin IPv4 adresleri (döngü ve 169.254.x.x hariç)
std::vector<std::string> localIPv4Addresses();

}  // namespace ekran
