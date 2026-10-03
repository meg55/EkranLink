// Bağlantı yönetimi + yayın oturumu. Konsol ve tepsi uygulaması bunu ortak kullanır.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <map>
#include <string>

#include <windows.h>

#include "display_power.h"

namespace ekran {

class Connection;

enum class LinkMode { Auto, Usb, Wifi };

struct Options {
    LinkMode mode = LinkMode::Auto;
    std::string ip;       // Wi-Fi'de sabit adres (boşsa ağda aranır)
    int output = 0;       // yansıtılacak PC ekranı (yansıtma modunda)
    bool extend = true;   // ikinci ekran: sürücü kuruluysa tablet için sanal monitör aç, yoksa yansıt
    bool onlyTablet = false;  // tek ekran: tablet bağlıyken diğer monitörler kapanır (yalnızca ikinci ekranda)
    bool autoConnect = true;    // tablet bulununca kendiliğinden bağlan (kapalıysa yalnızca connectNow ile)
    int rotation = -1;          // ikinci ekranın yönü: -1 tabletle birlikte döner, yoksa sabit (0/90/180/270)
    int resolution = 100;       // ikinci ekranın çözünürlüğü, tabletinkinin yüzdesi (50, 75, 100, 125, 150)
    bool touchAsMouse = false;  // dokunma = fare (varsayılan: gerçek çoklu dokunma)
    int fps = 60;         // en yüksek kare hızı (tabletin ekranı)
    bool lowLatency = false;  // düşük gecikme: itici kareler hareket sırasında da (tablet ~2 kat çalışır)
    int kbps = 8000;
    int pushers = 3;      // ekran değiştikten sonra gönderilen itici kare sayısı
    int pusherMs = 8;     // itici kareler arası süre
    int maxInFlight = 12; // onaysız en fazla kare (çözücüde bekleyenler + toplu onay dahil)
};

// Sürücünün sunduğu, tabletin en-boy oranını koruyan çözünürlükler (yüzde)
constexpr int kResolutions[] = {50, 75, 100, 125, 150};

// Görüntü modu (bağlıyken gerçekte kullanılan)
enum class DisplayMode { Extend = 0, Only = 1, Mirror = 2 };

// Kullanıcının bir şey yapması gereken durumlar (metni arayüz kendi dilinde yazar, i18n.h)
enum class Notice { AppMissing = 1, TabletOutdated = 2, PcOutdated = 3 };

struct SessionState {
    bool connected = false;
    std::string device, link;   // tablet adı; "USB" / "Wi-Fi 192.168.1.5"
    DisplayMode mode = DisplayMode::Extend;
    int width = 0, height = 0;  // tabletin ekranı (akış boyutu)
};

struct StreamerEvents {
    // Bağlandı / koptu / görüntü modu değişti
    std::function<void(const SessionState& s)> onState;
    // Wi-Fi'de ilk eşleştirme: kullanıcıya "tablette bu kod mu görünüyor?" diye sor (engelleyici).
    // cancel: tablet reddederse tetiklenen olay (soru kendiliğinden kapanmalı)
    std::function<bool(const std::string& device, const std::string& code, HANDLE cancel)> onPairRequest;
    // Saniyelik ölçüm satırı
    std::function<void(const std::string& line)> onStats;
    // Yön ya da çözünürlük Windows ekran ayarlarından değiştirildi ve kaydedildi: arayüz tazelensin
    std::function<void()> onSettingsChanged;
    // Kullanıcının bir şey yapması gereken durum (ör. tablette uygulama yok); bildirim olarak gösterilir
    // arg: PcOutdated'da tabletteki uygulama sürümü
    std::function<void(Notice n, const std::string& arg)> onNotice;
};

class Streamer {
public:
    Streamer(Options opt, StreamerEvents ev);
    ~Streamer();

    // stop() çağrılana kadar tableti arar, bağlanır, koparsa yeniden dener.
    void run();
    void stop();

    // Ayarlar değişti: bağlantı kopmadan uygulanır. Dokunma, performans, yön, çözünürlük ve ikinci ekran ↔
    // tek ekran anında; yansıtma ↔ ikinci ekran ve yansıtılan ekran görüntü yeniden kurularak (bağlantı sürer).
    // Bağlantı yalnızca bağlantı türü şimdiki bağlantıyı dışarıda bırakırsa (ör. USB'deyken "sadece Wi-Fi") kapanır.
    void update(const Options& o);
    // Otomatik bağlanma kapalıyken: 30 sn içinde bulunan tablete bir kez bağlan
    void connectNow();

private:
    // Tablete bağlanıp oturum bitene kadar yayın yapar. El sıkışma başarılıysa true.
    bool runSession(const std::string& host, const std::string& link, const std::wstring& adbPath = {});
    void setMode(LinkMode m);
    void sleepInterruptible(int ms);
    void notice(Notice n, const std::string& arg, const char* logText);

    Options opt_;  // değişmeyen ayarlar (kare hızı, bit hızı, ...); değişebilenler aşağıdaki atomiklerde
    StreamerEvents ev_;
    std::atomic<bool> stop_{false};
    std::atomic<LinkMode> mode_;
    std::atomic<bool> autoConnect_{true}, touchAsMouse_{false}, lowLatency_{false};
    std::atomic<int> display_{0};  // istenen DisplayMode
    std::atomic<int> output_{0};
    std::atomic<int> fixedRotation_{-1};
    std::atomic<int> resolution_{100};
    std::atomic<int> currentLink_{-1};  // şimdiki bağlantı: -1 yok, 0 Wi-Fi, 1 USB
    std::mutex connMutex_;
    Connection* conn_ = nullptr;
    HANDLE wake_;
    // Eşleştirmesi reddedilen tabletlere bir süre tekrar sorma (cihaz kimliği → µs)
    std::map<std::string, int64_t> rejectedUntil_;
    DisplayPower displayPower_;  // Windows ekranı kapatınca tablet de kapansın
    std::atomic<int64_t> manualUntilUs_{0};  // "Şimdi bağlan" süresi
    int lastNotice_ = 0;
    std::string incompatibleHost_;  // sürümü uyuşmayan tablet (adres) ve yeniden deneme zamanı
    int64_t incompatibleUntilUs_ = 0;
    int64_t lastNoticeUs_ = 0;
};

}  // namespace ekran
