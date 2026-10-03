// Ayar penceresi (Direct2D ile çizilir). Yalnızca açıkken var olur; kapatılınca tüm kaynakları bırakılır.
// Solda sayfa menüsü (Durum, Bağlantı, Görüntü, Dokunma ve performans, Genel); her sayfa kaydırmadan sığar.
// Tepsi uygulaması durumu Model olarak verir, kullanıcı seçimlerini act() ile alır.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "streamer.h"

namespace ekran::ui {

struct Resolution {
    int width = 0, height = 0;  // yatay yöndeki boyut
};

struct Model {
    Options opt;
    bool connected = false;
    std::wstring device, link;
    DisplayMode activeMode = DisplayMode::Extend;
    int deviceWidth = 0, deviceHeight = 0;  // bağlı cihazın ekranı = önerilen çözünürlük
    // Ölçümler (bağlıyken, saniyede bir): boşsa henüz yok
    std::wstring latency, data, fps;
    // Sanal monitörün Windows'taki modları (Windows ekran ayarlarındaki listenin aynısı) ve şimdiki
    // çözünürlük / yön. Bağlı değilken ya da yansıtmada boş.
    std::vector<Resolution> modes;
    Resolution current;
    int currentRotation = 0;
    // Çözülene kadar Durum sayfasında kalan uyarı (0: yok)
    Notice notice = Notice(0);
    std::wstring noticeArg;
    bool driver = false;        // sanal ekran sürücüsü kurulu (ikinci ekran / tek ekran)
    bool serviceMode = false;   // hizmet kurulu
    bool serviceStale = false;  // kurulu hizmet bu sürümden farklı
    bool autostart = false;
    int language = 0;           // 0 sistem, 1 Türkçe, 2 İngilizce
    std::vector<std::wstring> outputs;
};

enum class Act {
    Reload,       // pencere öne geldi: ayarları depodan yeniden oku
    Closed,       // pencere kapandı (ölçüm aktarımı durur)
    Link,         // LinkMode
    AutoConnect,  // 0/1
    ConnectNow,
    Display,      // DisplayMode
    Output,       // yansıtılan ekran sırası
    Rotation,     // -1 otomatik, 0/90/180/270
    Resolution,   // Model::modes sırası
    Touch,        // 0 dokunmatik, 1 fare
    LowLatency,   // 0/1
    Autostart,    // 0/1
    Service,      // kur / kaldır / güncelle
    Language,     // 0/1/2
    Forget,
    OpenLog,
    OpenDisplaySettings,
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
