// EkranLink sanal ekran sürücüsü (driver/) ile konuşur: tablet başına Windows'a gerçek bir monitör ekler/kaldırır.
#pragma once

#include <windows.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace ekran {

struct VirtualDisplaySpec {
    int width = 0, height = 0, refreshHz = 60;
    GUID monitorGuid{};     // aynı tablet için hep aynı → Windows yerleşimi/ölçeği hatırlar
    std::string name;       // EDID ürün adı (≤13)
    std::string serial;     // EDID seri no (≤13)
    int widthMm = 0, heightMm = 0;
};

class VirtualDisplay {
public:
    ~VirtualDisplay() { remove(); }

    // Sürücü kurulu ve çalışıyor mu?
    static bool driverAvailable();
    // Cihaz kimliğinden sabit bir monitör GUID'i üretir.
    static GUID guidFor(const std::string& deviceId);

    // Monitörü ekler; Windows etkinleştirince GDI adını (\\.\DISPLAYn) döndürür. timeoutMs içinde olmazsa false.
    bool add(const VirtualDisplaySpec& spec, std::wstring& gdiName, int timeoutMs = 8000);
    void remove();
    // Monitörün yönü (0, 90, 180, 270): Windows masaüstünü döndürür, panel yatay kalır.
    bool setRotation(int degrees);
    // Çözünürlük (yatay yöndeki boyut; dikeyde yer değiştirir). Sürücü tabletin boyutunun %50/75/100/125/150'sini
    // ve standart modları sunar; monitör kaldırılmadan değişir.
    bool setResolution(int width, int height);
    // Şimdiki çözünürlük, yatay yöndeki boyut olarak (bilinmiyorsa false)
    bool resolution(int& width, int& height) const;
    // Tek ekran: EkranLink dışındaki monitörleri geçici kapatır, bu cihaz ana ekran olur. remove() eski düzeni
    // geri yükler. Birden çok cihaz bağlıyken düzen süreç genelinde ortaktır: ilk tek ekrana geçen kaydeder, son
    // çıkan geri yükler; diğer EkranLink monitörleri açık kalır. Kalıcı kaydedilmez: program çökerse sürücünün
    // bekçi köpeği monitörleri kaldırır, Windows da kayıtlı (cihazsız) düzene döner.
    bool isolate();
    // Tek ekrandan çık: son çıkan cihazsa diğer monitörler geri gelir (cihazların şimdiki çözünürlüğü ve yönü
    // korunur)
    void restoreLayout();
    bool isolated() const { return isolated_; }
    bool added() const { return added_; }

private:
    HANDLE dev_ = INVALID_HANDLE_VALUE;
    GUID guid_{};
    std::wstring gdiName_;
    LUID adapter_{};
    UINT targetId_ = 0;
    bool isolated_ = false;

    DWORD persistFlags() const;
    bool added_ = false;
    std::atomic<bool> pinging_{false};
    std::thread pinger_;
};

}  // namespace ekran
