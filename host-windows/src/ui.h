// Ayar penceresi (Direct2D ile çizilir). Yalnızca açıkken var olur; kapatılınca tüm kaynakları bırakılır.
// Tepsi uygulaması durumu Model olarak verir, kullanıcı seçimlerini act() ile alır.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "streamer.h"

namespace ekran::ui {

struct Model {
    Options opt;
    bool connected = false;
    std::wstring device, link, stats;
    DisplayMode activeMode = DisplayMode::Extend;
    bool driver = false;        // sanal ekran sürücüsü kurulu (ikinci ekran / tek ekran)
    bool serviceMode = false;   // hizmet kurulu
    bool serviceStale = false;  // kurulu hizmet bu sürümden farklı
    bool autostart = false;
    int language = 0;           // 0 sistem, 1 Türkçe, 2 İngilizce
    std::vector<std::wstring> outputs;
};

enum class Act {
    Reload,       // pencere öne geldi: ayarları depodan yeniden oku
    Link,         // LinkMode
    AutoConnect,  // 0/1
    ConnectNow,
    Display,      // DisplayMode
    Output,       // yansıtılan ekran sırası
    Rotation,     // -1 otomatik, 0/90/180/270
    Touch,        // 0 dokunmatik, 1 fare
    LowLatency,   // 0/1
    Autostart,    // 0/1
    Service,      // kur / kaldır / güncelle
    Language,     // 0/1/2
    Forget,
    OpenLog,
};

class Host {
public:
    virtual ~Host() = default;
    virtual Model model() = 0;
    virtual void act(Act a, int value) = 0;
};

void show(Host* host, HICON icon);  // açıksa öne getirir
void refresh();                     // durum ya da ayar değişti
void close();
HWND window();                      // açık değilse nullptr (ileti kutularının sahibi)

}  // namespace ekran::ui
