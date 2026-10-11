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

// Listede bir cihaz: bağlı ya da bağlanılabilir
struct DeviceView {
    std::wstring name, link;
    DeviceState state = DeviceState::Found;
    DisplayMode mode = DisplayMode::Extend;
    // Bağlıyken: ölçümler (saniyede bir; boşsa henüz yok), monitörün şimdiki çözünürlüğü ve yönü
    std::wstring latency, data, fps;
    Resolution current;
    int rotation = 0;
    int width = 0, height = 0;  // cihazın ekranı = önerilen çözünürlük
};

struct Model {
    Options opt;
    std::vector<DeviceView> devices;  // bağlılar önce
    int connected = 0;                // bağlı cihaz sayısı
    // Görüntü sayfasında ayarları gösterilen bağlı cihaz (devices sırası; yoksa -1) ve onun monitörünün
    // Windows'taki modları (Windows ekran ayarlarındaki listenin aynısı). Yansıtmada boş.
    int displayDevice = -1;
    std::vector<Resolution> modes;
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
    Reload,         // pencere öne geldi: ayarları depodan yeniden oku
    Closed,         // pencere kapandı (ölçüm aktarımı ve cihaz arama durur)
    Link,           // LinkMode
    AutoConnect,    // 0/1
    Connect,        // Model::devices sırası
    Disconnect,     // Model::devices sırası (-1: hepsi)
    DisplayDevice,  // Görüntü sayfasındaki cihaz (Model::devices sırası)
    Display,        // DisplayMode
    Output,         // yansıtılan ekran sırası
    Rotation,       // -1 otomatik, 0/90/180/270
    Resolution,     // Model::modes sırası
    Touch,          // 0 dokunmatik, 1 fare
    LowLatency,     // 0/1
    Autostart,      // 0/1
    Service,        // kur / kaldır / güncelle
    Language,       // 0/1/2
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
