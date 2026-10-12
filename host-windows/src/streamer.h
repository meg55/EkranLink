// Cihaz bulma + bağlantı yönetimi + yayın oturumları. Konsol, tepsi uygulaması ve hizmet ajanı ortak kullanır.
// Birden çok cihaz aynı anda bağlanabilir: her biri kendi oturumunda (iş parçacığı), ikinci ekranda kendi sanal
// monitörüyle.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <windows.h>

#include "display_power.h"

namespace ekran {

class Connection;

enum class LinkMode { Auto, Usb, Wifi };

struct Options {
    LinkMode mode = LinkMode::Auto;
    std::string ip;       // Wi-Fi'de sabit adres (boşsa ağda aranır)
    std::string serial;   // USB'de yalnızca bu adb cihazı (boşsa hepsi)
    int output = 0;       // yansıtılacak PC ekranı (yansıtma modunda)
    bool extend = true;   // ikinci ekran: sürücü kuruluysa cihaz için sanal monitör aç, yoksa yansıt
    bool onlyTablet = false;  // tek ekran: cihaz bağlıyken diğer monitörler kapanır (yalnızca ikinci ekranda)
    bool autoConnect = true;    // cihaz bulununca kendiliğinden bağlan (kapalıysa yalnızca connect ile)
    int rotation = -1;          // ikinci ekranın yönü: -1 cihazla birlikte döner, yoksa sabit (0/90/180/270)
    bool touchAsMouse = false;  // dokunma = fare (varsayılan: gerçek çoklu dokunma)
    int fps = 60;         // en yüksek kare hızı (cihazın ekranı)
    bool lowLatency = false;  // düşük gecikme: itici kareler hareket sırasında da (cihaz ~2 kat çalışır)
    int kbps = 8000;
    int pushers = 3;      // ekran değiştikten sonra gönderilen itici kare sayısı
    int pusherMs = 8;     // itici kareler arası süre
    int maxInFlight = 12; // onaysız en fazla kare (çözücüde bekleyenler + toplu onay dahil)
};

// Görüntü modu (bağlıyken gerçekte kullanılan)
enum class DisplayMode { Extend = 0, Only = 1, Mirror = 2 };

// Kullanıcının bir şey yapması gereken durumlar (metni arayüz kendi dilinde yazar, i18n.h)
enum class Notice { AppMissing = 1, TabletOutdated = 2, PcOutdated = 3, Repair = 4 /* arg: cihaz adı */ };

enum class DeviceState {
    Found = 0,       // bağlanılabilir
    Connecting = 1,  // uygulama açılıyor / el sıkışma
    Connected = 2,
    WaitingApp = 3,  // USB: cihazdaki "otomatik aç" kapalı ya da kullanıcı cihazdan kesti; cihazda açılması bekleniyor
    AppMissing = 4,  // USB: cihazda EkranLink yok
};

struct DeviceInfo {
    std::string key;       // "usb:SERİ" ya da "wifi:KİMLİK": değişmez, komutlar (bağlan / kes) bununla verilir
    std::string name;      // "LENOVO Vodafone Smart Tab III 10"
    std::string link;      // "USB" / "Wi-Fi 192.168.1.5"
    std::string deviceId;  // cihazın kendi kimliği (biliniyorsa): cihaza özel ayarlar (çözünürlük)
    DeviceState state = DeviceState::Found;
    // Bağlıyken
    DisplayMode mode = DisplayMode::Extend;
    int width = 0, height = 0;  // cihazın ekranı (akış boyutu) = önerilen çözünürlük
    std::wstring gdiName;       // ikinci ekranda sanal monitörün Windows adı (\\.\DISPLAYn), yansıtmada boş
    bool operator==(const DeviceInfo&) const = default;
};

struct StreamerEvents {
    // Cihaz listesi değişti (bulundu, bağlandı, koptu, görüntü modu değişti). Bağlılar önce.
    std::function<void(const std::vector<DeviceInfo>&)> onDevices;
    // Wi-Fi'de ilk eşleştirme: kullanıcıya "cihazda bu kod mu görünüyor?" diye sor (engelleyici).
    // cancel: cihaz reddederse tetiklenen olay (soru kendiliğinden kapanmalı)
    std::function<bool(const std::string& device, const std::string& code, HANDLE cancel)> onPairRequest;
    // Bağlı cihazın saniyelik ölçüm satırı
    std::function<void(const std::string& key, const std::string& line)> onStats;
    // Yön ya da çözünürlük Windows ekran ayarlarından değiştirildi ve kaydedildi: arayüz tazelensin
    std::function<void()> onSettingsChanged;
    // Kullanıcının bir şey yapması gereken durum (ör. cihazda uygulama yok); bildirim olarak gösterilir
    // arg: PcOutdated'da cihazdaki uygulama sürümü
    std::function<void(Notice n, const std::string& arg)> onNotice;
};

class Streamer {
public:
    Streamer(Options opt, StreamerEvents ev);
    ~Streamer();

    // stop() çağrılana kadar cihazları arar ve oturumları yönetir.
    void run();
    void stop();

    // Ayarlar değişti: bağlantılar kopmadan uygulanır. Dokunma, performans, yön, çözünürlük (cihaza özel,
    // config.h) ve ikinci ekran ↔ tek ekran anında; yansıtma ↔ ikinci ekran ve yansıtılan ekran görüntü yeniden
    // kurularak. Bir bağlantı yalnızca bağlantı türü onu dışarıda bırakırsa (ör. USB'deyken "sadece Wi-Fi") kapanır.
    void update(const Options& o);
    // Bu cihaza bağlan (otomatik bağlanma kapalıyken ya da bağlantısı kesilmiş cihaz için). Cihaz uygulamayı
    // açmazsa bir dakika boyunca denenir.
    void connect(const std::string& key);
    // Bağlantıyı kes (key boşsa hepsi). Kullanıcı yeniden bağlanana ya da cihaz çıkarılana kadar otomatik
    // bağlanılmaz.
    void disconnect(const std::string& key);
    // Arayüz açık: otomatik bağlanma kapalıyken de cihazlar aranır (listede görünsün)
    void setWatching(bool on);

private:
    struct Dev;
    enum class Result { Failed, Ended };

    void scanUsb(const std::wstring& adbPath, bool usbWatchOk);
    void scanWifi();
    void startSessions(const std::wstring& adbPath);
    void prune();
    void sessionWorker(Dev* d, std::wstring adbPath);
    Result runSession(Dev& d, const std::string& host, unsigned port, const std::wstring& adbPath);
    void publish();
    bool suppressedLocked(const Dev& d) const;
    bool linkAllowed(const Dev& d) const;
    void notice(Notice n, const std::string& arg, const char* logText);

    Options opt_;  // değişmeyen ayarlar (kare hızı, bit hızı, ...); değişebilenler aşağıdaki atomiklerde
    StreamerEvents ev_;
    std::atomic<bool> stop_{false};
    std::atomic<LinkMode> mode_;
    std::atomic<bool> autoConnect_{true}, touchAsMouse_{false}, lowLatency_{false}, watching_{false};
    std::atomic<bool> rescan_{true};  // hemen tara (arayüz açıldı, bağlantı türü değişti)
    std::atomic<int> display_{0};  // istenen DisplayMode
    std::atomic<int> output_{0};
    std::atomic<int> fixedRotation_{-1};
    std::atomic<int> settingsGen_{0};  // her update()'te artar: cihaza özel ayarlar yeniden okunur
    HANDLE wake_;       // tarayıcıyı uyandır (otomatik sıfırlanır)
    HANDLE stopEvent_;  // oturum iş parçacıkları için (elle sıfırlanır)
    DisplayPower displayPower_;  // Windows ekranı kapatınca cihaz da kapansın

    std::mutex m_;  // aşağıdakiler
    std::map<std::string, std::unique_ptr<Dev>> devs_;
    std::map<std::string, int64_t> wanted_;  // elle bağlan istenen cihaz → isteğin bitişi (µs)
    std::set<std::string> held_;             // kullanıcı bağlantısını kesti: otomatik bağlanma
    std::map<std::string, unsigned> usbPorts_;      // USB seri → PC'deki yerel port (adb forward)
    std::vector<DeviceInfo> published_;
    int64_t nextUsbPollUs_ = 0, usbBurstUntilUs_ = 0, nextWifiUs_ = 0;
    int64_t pairQuietUntilUs_ = 0;  // ret sonrası: kendiliğinden eşleştirme sorusu açılmaz

    std::mutex publishMutex_;  // yayın sırası (m_'den önce alınır)
    std::mutex noticeMutex_;
    int lastNotice_ = 0;
    int64_t lastNoticeUs_ = 0;
};

}  // namespace ekran
