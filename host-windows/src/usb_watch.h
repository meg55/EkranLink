// USB'ye ADB aygıtı (hata ayıklaması açık bir Android) takılınca haber verir. Böylece tablet takılı değilken
// birkaç saniyede bir adb çalıştırmak gerekmez; boşta işlemci yükü sıfır.
#pragma once

#include <windows.h>

namespace ekran {

class UsbWatch {
public:
    UsbWatch();
    ~UsbWatch();
    bool ok() const { return notify_ != nullptr; }  // bildirim alınamıyorsa eskisi gibi yoklanır
    HANDLE event() const { return event_; }         // takılınca/çıkınca tetiklenir (kendiliğinden sıfırlanır)

private:
    HANDLE event_ = nullptr;
    void* notify_ = nullptr;
};

}  // namespace ekran
