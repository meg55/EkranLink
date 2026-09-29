// EKRAN sanal ekran sürücüsü (driver/) ile konuşur: tablet başına Windows'a gerçek bir monitör ekler/kaldırır.
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
    // Monitörün yönü (0, 90, 180, 270): tablet dikey tutulunca Windows masaüstünü döndürür, panel yatay kalır.
    // Kalıcı kaydedilmez; her bağlantıda tablet bildirir.
    bool setRotation(int degrees);
    // Tek ekran: diğer monitörleri geçici kapatır, tablet ana ekran olur. remove() eski düzeni geri yükler.
    // Kalıcı kaydedilmez: program çökerse sürücünün bekçi köpeği monitörü kaldırır, Windows da kayıtlı
    // (tabletsiz) düzene döner.
    bool isolate();

private:
    HANDLE dev_ = INVALID_HANDLE_VALUE;
    GUID guid_{};
    std::wstring gdiName_;
    LUID adapter_{};
    UINT targetId_ = 0;
    bool isolated_ = false;
    std::vector<DISPLAYCONFIG_PATH_INFO> savedPaths_;
    std::vector<DISPLAYCONFIG_MODE_INFO> savedModes_;

    void restoreLayout();
    bool added_ = false;
    std::atomic<bool> pinging_{false};
    std::thread pinger_;
};

}  // namespace ekran
