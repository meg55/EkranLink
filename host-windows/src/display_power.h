// Windows'un ekranları kapatıp açmasını izler (güç ayarı bildirimi; yoklama yok, pencere gerekmez).
// Ekran kapanınca tablete haber verilir, tablet de ekranını kapatır.
#pragma once

#include <windows.h>

#include <atomic>

namespace ekran {

class DisplayPower {
public:
    ~DisplayPower() { stop(); }
    bool start();
    void stop();
    // Windows ekranları açık mı (kısılmış = açık)
    bool on() const { return state_.load() != 0; }

private:
    static ULONG CALLBACK onChange(PVOID ctx, ULONG type, PVOID setting);
    std::atomic<int> state_{1};
    void* handle_ = nullptr;
    void* params_[2] = {};  // DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS { geri çağırma, bağlam }; kayıt süresince yaşamalı
};

}  // namespace ekran
