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
    bool touchAsMouse = false;  // dokunma = fare (varsayılan: gerçek çoklu dokunma)
    int fps = 60;         // en yüksek kare hızı (tabletin ekranı)
    bool lowLatency = false;  // düşük gecikme: itici kareler hareket sırasında da (tablet ~2 kat çalışır)
    int kbps = 8000;
    int pushers = 3;      // ekran değiştikten sonra gönderilen itici kare sayısı
    int pusherMs = 8;     // itici kareler arası süre
    int maxInFlight = 12; // onaysız en fazla kare (çözücüde bekleyenler + toplu onay dahil)
};

// Görüntü modu (bağlıyken gerçekte kullanılan)
enum class DisplayMode { Extend = 0, Only = 1, Mirror = 2 };

// Kullanıcının bir şey yapması gereken durumlar (metni arayüz kendi dilinde yazar, i18n.h)
enum class Notice { AppMissing = 1, TabletOutdated = 2, PcOutdated = 3 };

struct StreamerEvents {
    // Bağlandı / koptu. device: tablet adı, link: "USB" / "Wi-Fi 192.168.1.5"
    std::function<void(bool connected, const std::string& device, const std::string& link, DisplayMode mode)> onState;
    // Wi-Fi'de ilk eşleştirme: kullanıcıya "tablette bu kod mu görünüyor?" diye sor (engelleyici).
    // cancel: tablet reddederse tetiklenen olay (soru kendiliğinden kapanmalı)
    std::function<bool(const std::string& device, const std::string& code, HANDLE cancel)> onPairRequest;
    // Saniyelik ölçüm satırı
    std::function<void(const std::string& line)> onStats;
    // Sabit yön Windows ekran ayarlarından değiştirildi (yeni değer kaydedildi): arayüz tazelensin
    std::function<void(int rotation)> onRotationChanged;
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

    // Çalışırken bağlantı türünü değiştir (mevcut oturum kapanır, yenisiyle aranır).
    void setMode(LinkMode m);
    // Otomatik bağlanma kapalıyken: 30 sn içinde bulunan tablete bir kez bağlan
    void connectNow();
    // İkinci ekranın yönü (-1: tabletle döner); bağlıyken hemen uygulanır
    void setRotation(int degrees) { fixedRotation_ = degrees; }

private:
    // Tablete bağlanıp oturum bitene kadar yayın yapar. El sıkışma başarılıysa true.
    bool runSession(const std::string& host, const std::string& link, const std::wstring& adbPath = {});
    void sleepInterruptible(int ms);
    void notice(Notice n, const std::string& arg, const char* logText);

    Options opt_;
    StreamerEvents ev_;
    std::atomic<bool> stop_{false};
    std::atomic<LinkMode> mode_;
    std::mutex connMutex_;
    Connection* conn_ = nullptr;
    HANDLE wake_;
    // Eşleştirmesi reddedilen tabletlere bir süre tekrar sorma (cihaz kimliği → µs)
    std::map<std::string, int64_t> rejectedUntil_;
    DisplayPower displayPower_;  // Windows ekranı kapatınca tablet de kapansın
    std::atomic<int64_t> manualUntilUs_{0};  // "Şimdi bağlan" süresi
    std::atomic<int> fixedRotation_{-1};
    int lastNotice_ = 0;
    std::string incompatibleHost_;  // sürümü uyuşmayan tablet (adres) ve yeniden deneme zamanı
    int64_t incompatibleUntilUs_ = 0;
    int64_t lastNoticeUs_ = 0;
};

}  // namespace ekran
