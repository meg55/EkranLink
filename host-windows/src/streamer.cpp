// Hat: DXGI yakalama → GPU video işlemci (ölçek + NV12) → donanım H.264 → TCP → cihaz.
// Görüntü hiç CPU belleğine inmez. Ekran değişmiyorsa kodlama da gönderim de yapılmaz.
//
// Tarayıcı (run) USB'deki (adb) ve Wi-Fi'deki (UDP keşif) cihazları listeler; bağlanılacak her cihaz için ayrı
// bir oturum iş parçacığı açar. Oturumlar birbirinden bağımsızdır: her biri kendi yakalama, kodlayıcı ve
// (ikinci ekranda) sanal monitörüyle çalışır.

#include "streamer.h"

#include <winsock2.h>
#include <psapi.h>

#include <algorithm>
#include <cstdio>
#include <thread>

#include "adb.h"
#include "capture.h"
#include "com.h"
#include "config.h"
#include "desktop.h"
#include "discovery.h"
#include "encoder.h"
#include "input.h"
#include "net.h"
#include "pairing.h"
#include "protocol.h"
#include "scaler.h"
#include "usb_watch.h"
#include "virtual_display.h"

namespace ekran {

namespace {

// Tanı: EKRAN_BELLEK=1 ise oturum kurulurken her adımdaki özel belleği günlüğe yaz
void logMemory(const char* stage) {
    static const bool on = GetEnvironmentVariableA("EKRAN_BELLEK", nullptr, 0) > 0;
    if (!on) return;
    PROCESS_MEMORY_COUNTERS_EX m{};
    m.cb = sizeof m;
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof m);
    logf("[bellek] %-22s özel %6.1f MB  çalışma %6.1f MB", stage, m.PrivateUsage / 1048576.0, m.WorkingSetSize / 1048576.0);
}
// Yüksek çözünürlüklü bekleme: sistem saat hassasiyetini (timeBeginPeriod) değiştirmeden.
class Waiter {
public:
    Waiter() {
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
        if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    ~Waiter() { CloseHandle(timer_); }
    void waitUs(int64_t us) {
        if (us <= 0) return;
        LARGE_INTEGER due;
        due.QuadPart = -us * 10;
        SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer_, INFINITE);
    }

private:
    HANDLE timer_;
};

struct Stats {
    std::atomic<int> frames{0}, pushers{0}, acks{0};
    std::atomic<int64_t> bytes{0}, e2eSumUs{0}, decodeSumUs{0};
    std::atomic<int64_t> rttUs{0};
};

bool makeNv12Pool(ID3D11Device* dev, int w, int h, ComPtr<ID3D11Texture2D> (&pool)[4]) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(w);
    td.Height = UINT(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_NV12;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_VIDEO_ENCODER;
    for (auto& t : pool) {
        if (FAILED(dev->CreateTexture2D(&td, nullptr, t.put()))) {
            td.BindFlags = D3D11_BIND_RENDER_TARGET;  // eski sürücüler VIDEO_ENCODER bayrağını tanımayabilir
            if (!check(dev->CreateTexture2D(&td, nullptr, t.put()), "NV12 dokusu")) return false;
        }
    }
    return true;
}

constexpr int64_t kSecond = 1'000'000;
constexpr int64_t kGoneUs = 10 * kSecond;  // bu kadar görünmeyen cihaz listeden çıkar (kesme bekletmesi de biter)
constexpr int64_t kWantUs = 60 * kSecond;  // "bağlan" isteği cihaz uygulamayı açana kadar bu kadar sürer

}  // namespace

// Bulunan bir cihaz ve (varsa) oturumu. m_ altında; busy iken oturum iş parçacığı da kullanır.
struct Streamer::Dev {
    DeviceInfo info;
    bool usb = false;
    std::string serial, ip;
    bool present = false;
    int64_t lastSeenUs = 0;
    int64_t retryAtUs = 0;  // bundan önce yeniden denenmez
    int64_t probeAtUs = 0;  // USB: uygulama yoksa yeniden sorma zamanı
    bool probed = false;
    int fails = 0;
    bool authFailed = false;  // kayıtlı anahtar doğrulanamadı: kullanıcı onayıyla yeniden eşleştirilir
    std::atomic<bool> busy{false};  // oturum iş parçacığı çalışıyor
    bool cancel = false;            // kullanıcı kesti: oturum kurulurken vazgeç
    std::thread worker;
    Connection* conn = nullptr;     // bağlıyken (kesmek için)
};

Streamer::Streamer(Options opt, StreamerEvents ev)
    : opt_(std::move(opt)),
      ev_(std::move(ev)),
      mode_(opt_.mode),
      wake_(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
      stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    update(opt_);
}

Streamer::~Streamer() {
    CloseHandle(wake_);
    CloseHandle(stopEvent_);
}

void Streamer::stop() {
    stop_ = true;
    SetEvent(stopEvent_);
    SetEvent(wake_);
    std::lock_guard<std::mutex> lock(m_);
    for (auto& [k, d] : devs_)
        if (d->conn) d->conn->close();
}

void Streamer::update(const Options& o) {
    autoConnect_ = o.autoConnect;
    touchAsMouse_ = o.touchAsMouse;
    lowLatency_ = o.lowLatency;
    display_ = int(!o.extend ? DisplayMode::Mirror : o.onlyTablet ? DisplayMode::Only : DisplayMode::Extend);
    output_ = o.output;
    fixedRotation_ = o.rotation;
    settingsGen_++;
    if (o.mode != mode_) {
        mode_ = o.mode;
        rescan_ = true;
        // Bağlantı türünün dışarıda bıraktığı oturumlar kapanır (ör. USB'deyken "sadece Wi-Fi")
        std::lock_guard<std::mutex> lock(m_);
        for (auto& [k, d] : devs_)
            if (d->conn && !linkAllowed(*d)) d->conn->close();
    }
    SetEvent(wake_);
}

void Streamer::connect(const std::string& key) {
    {
        std::lock_guard<std::mutex> lock(m_);
        held_.erase(key);
        wanted_[key] = nowUs() + kWantUs;
        auto it = devs_.find(key);
        if (it != devs_.end()) {
            Dev& d = *it->second;
            d.retryAtUs = 0;
            d.fails = 0;
            d.cancel = false;
            if (d.info.state == DeviceState::AppMissing) d.probeAtUs = 0;  // kurulmuş olabilir: yeniden sor
        }
    }
    rescan_ = true;
    SetEvent(wake_);
}

void Streamer::disconnect(const std::string& key) {
    {
        std::lock_guard<std::mutex> lock(m_);
        for (auto& [k, d] : devs_) {
            if (!key.empty() && k != key) continue;
            if (!key.empty() || d->busy) held_.insert(k);
            wanted_.erase(k);
            resume_.erase(k);
            d->cancel = true;
            if (d->conn) {
                logf("Bağlantı kullanıcı tarafından kesiliyor: %s", d->info.name.c_str());
                d->conn->close();
            }
        }
        if (key.empty()) {
            wanted_.clear();
            resume_.clear();
        }
    }
    SetEvent(wake_);
}

void Streamer::setWatching(bool on) {
    if (watching_.exchange(on) == on) return;
    if (on) rescan_ = true;
    SetEvent(wake_);
}

bool Streamer::linkAllowed(const Dev& d) const {
    const LinkMode m = mode_;
    return m == LinkMode::Auto || (m == LinkMode::Usb) == d.usb;
}

// Aynı cihaz hem USB'de hem Wi-Fi'de görünüyorsa tek kayıt: bağlı olan, yoksa USB
bool Streamer::suppressedLocked(const Dev& d) const {
    if (d.busy || d.info.deviceId.empty()) return false;
    for (const auto& [k, o] : devs_) {
        if (o.get() == &d || o->usb == d.usb || o->info.deviceId != d.info.deviceId) continue;
        if (o->busy) return true;                       // diğer bağlantıdan bağlı
        if (!d.usb && o->present && linkAllowed(*o)) return true;  // ikisi de boşta: USB tercih
    }
    return false;
}

void Streamer::notice(Notice n, const std::string& arg, const char* logText) {
    // Bağlantı birkaç saniyede bir yeniden denenir: aynı uyarı 10 dakikada bir
    {
        std::lock_guard<std::mutex> lock(noticeMutex_);
        const int64_t now = nowUs();
        if (int(n) == lastNotice_ && now < lastNoticeUs_ + 600 * kSecond) return;
        lastNotice_ = int(n);
        lastNoticeUs_ = now;
    }
    logf("%s", logText);
    if (ev_.onNotice) ev_.onNotice(n, arg);
}

void Streamer::publish() {
    std::lock_guard<std::mutex> pub(publishMutex_);
    std::vector<DeviceInfo> list;
    {
        std::lock_guard<std::mutex> lock(m_);
        for (const auto& [k, d] : devs_) {
            if (!d->present && !d->busy) continue;
            if (!d->busy && (!linkAllowed(*d) || suppressedLocked(*d))) continue;
            list.push_back(d->info);
        }
        std::stable_sort(list.begin(), list.end(), [](const DeviceInfo& a, const DeviceInfo& b) {
            const bool ca = a.state == DeviceState::Connected, cb = b.state == DeviceState::Connected;
            if (ca != cb) return ca;
            return a.name < b.name;
        });
        if (list == published_) return;
        published_ = list;
    }
    if (ev_.onDevices) ev_.onDevices(list);
}

void Streamer::scanUsb(const std::wstring& adbPath, bool usbWatchOk) {
    // Cihaz takılı değilken adb çalıştırılmaz: Windows'un takılma bildirimi beklenir (bildirim yoksa 3 sn'de
    // bir); takıldıktan sonraki birkaç saniye adb'nin görmesi beklenir.
    const int64_t now = nowUs();
    if (now < nextUsbPollUs_) return;
    nextUsbPollUs_ = now + (now < usbBurstUntilUs_ ? kSecond : usbWatchOk ? 30 * kSecond : 3 * kSecond);
    std::vector<std::string> serials = adb::devices(adbPath);
    if (!opt_.serial.empty())
        serials.erase(std::remove_if(serials.begin(), serials.end(), [&](const std::string& s) { return s != opt_.serial; }),
                      serials.end());

    std::vector<Dev*> toProbe;
    {
        std::lock_guard<std::mutex> lock(m_);
        for (auto& [k, d] : devs_)
            if (d->usb && std::find(serials.begin(), serials.end(), d->serial) == serials.end()) d->present = false;
        for (const auto& s : serials) {
            const std::string key = "usb:" + s;
            auto& slot = devs_[key];
            if (!slot) {
                slot = std::make_unique<Dev>();
                slot->usb = true;
                slot->serial = s;
                slot->info.key = key;
                slot->info.link = "USB";
                slot->info.name = s;
            }
            slot->present = true;
            slot->lastSeenUs = now;
            if (!slot->busy && (!slot->probed || (slot->info.state == DeviceState::AppMissing && now >= slot->probeAtUs)))
                toProbe.push_back(slot.get());
        }
    }
    // Cihazı tanı (uygulamayı açmadan): kimlik ve ad; uygulama yoksa 10 sn'de bir yeniden sorulur
    for (Dev* d : toProbe) {
        std::string id, name;
        const adb::App app = adb::probe(adbPath, d->serial, id, name);
        bool missingNow = false, wanted = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (!proto::cleanName(name).empty()) d->info.name = proto::cleanName(name);
            if (proto::validId(id)) d->info.deviceId = id;
            if (app == adb::App::Installed) {
                d->probed = true;
                if (d->info.state == DeviceState::AppMissing) d->info.state = DeviceState::Found;
            } else if (app == adb::App::Missing) {
                missingNow = d->info.state != DeviceState::AppMissing;
                d->info.state = DeviceState::AppMissing;
                d->probeAtUs = now + 10 * kSecond;
                wanted = (autoConnect_ && !held_.count(d->info.key)) || wanted_.count(d->info.key);
            } else {
                d->probeAtUs = now + 3 * kSecond;  // ör. cihazda "USB hata ayıklamaya izin ver" bekleniyor
                nextUsbPollUs_ = std::min(nextUsbPollUs_, now + 3 * kSecond);
            }
        }
        if (missingNow) {
            logf("USB: %s cihazında EkranLink uygulaması yok; APK cihaza ayrıca kurulmalı", d->info.name.c_str());
            if (wanted) notice(Notice::AppMissing, {}, "USB: cihazda EkranLink uygulaması yok");
        }
    }
    if (!toProbe.empty()) nextUsbPollUs_ = std::min(nextUsbPollUs_, now + 10 * kSecond);  // uygulama kurulabilir
}

void Streamer::scanWifi() {
    const int64_t now = nowUs();
    if (now < nextWifiUs_) return;
    std::vector<std::string> known = config::knownAddresses();
    if (!opt_.ip.empty()) known.insert(known.begin(), opt_.ip);
    const std::vector<FoundDevice> found = discoverDevices(1000, known);
    const int64_t after = nowUs();
    bool anyConnected = false;
    {
        std::lock_guard<std::mutex> lock(m_);
        size_t wifiCount = 0;
        for (const auto& [k, d] : devs_) wifiCount += !d->usb;
        for (const auto& f : found) {
            if (!proto::validId(f.id)) continue;  // kimliksiz / bozuk cevap
            const std::string key = "wifi:" + f.id;
            auto& slot = devs_[key];
            if (!slot) {
                if (wifiCount >= 16) {  // ağdan sahte cevap yağmuruna karşı
                    devs_.erase(key);
                    continue;
                }
                wifiCount++;
                slot = std::make_unique<Dev>();
                slot->info.key = key;
            }
            slot->ip = f.ip;
            if (!slot->busy) slot->info.link = "Wi-Fi " + f.ip;
            const std::string name = proto::cleanName(f.name);
            if (!name.empty()) slot->info.name = name;
            slot->info.deviceId = f.id;
            slot->present = true;
            slot->lastSeenUs = after;
        }
        for (auto& [k, d] : devs_) {
            if (!d->usb && d->present && after - d->lastSeenUs > kGoneUs) d->present = false;
            anyConnected |= d->busy;
        }
    }
    // Bağlıyken ve pencere kapalıyken yeni cihaz için daha seyrek bak (keşif çok hafif ama gereksiz)
    nextWifiUs_ = after + (anyConnected && !watching_ ? 10 : 3) * kSecond;
}

void Streamer::prune() {
    std::vector<std::thread> done;
    {
        std::lock_guard<std::mutex> lock(m_);
        const int64_t now = nowUs();
        for (auto it = devs_.begin(); it != devs_.end();) {
            Dev& d = *it->second;
            if (!d.busy && d.worker.joinable()) done.push_back(std::move(d.worker));
            if (!d.present && !d.busy && now - d.lastSeenUs > kGoneUs) {
                // Cihaz çıkarıldı / ağdan ayrıldı: kesme bekletmesi ve bağlan isteği de biter
                held_.erase(it->first);
                wanted_.erase(it->first);
                resume_.erase(it->first);
                it = devs_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = wanted_.begin(); it != wanted_.end();) it = it->second < now ? wanted_.erase(it) : std::next(it);
        for (auto it = resume_.begin(); it != resume_.end();) it = it->second < now ? resume_.erase(it) : std::next(it);
    }
    for (auto& t : done) t.join();
}

void Streamer::startSessions(const std::wstring& adbPath) {
    std::lock_guard<std::mutex> lock(m_);
    const int64_t now = nowUs();
    const bool autoC = autoConnect_;
    for (auto& [k, dp] : devs_) {
        Dev& d = *dp;
        if (!d.present || d.busy || now < d.retryAtUs || d.info.state == DeviceState::AppMissing) continue;
        if (!linkAllowed(d) || suppressedLocked(d)) continue;
        if (d.usb && adbPath.empty()) continue;
        if (!((autoC && !held_.count(k)) || wanted_.count(k) || resume_.count(k))) continue;
        // Ağdaki eşleşmemiş cihaza kendiliğinden bağlanılmaz: listede görünür, kullanıcı "Bağlan" der
        if (!d.usb && !wanted_.count(k) && !pairing::known(d.info.deviceId)) continue;
        if (d.worker.joinable()) continue;  // önceki iş parçacığı henüz toplanmadı (prune)
        d.busy = true;
        d.cancel = false;
        if (d.info.state != DeviceState::WaitingApp) d.info.state = DeviceState::Connecting;
        d.worker = std::thread(&Streamer::sessionWorker, this, &d, adbPath);
    }
}

void Streamer::run() {
    if (!displayPower_.start()) logf("Ekran güç bildirimi alınamıyor; uyku eşitleme kapalı");
    UsbWatch usb;
    std::wstring adbPath;
    int64_t nextAdbLookupUs = 0;
    bool warnedNoAdb = false;

    while (!stop_) {
        if (rescan_.exchange(false)) nextUsbPollUs_ = nextWifiUs_ = 0;
        prune();
        bool wantedAny;
        {
            std::lock_guard<std::mutex> lock(m_);
            wantedAny = !wanted_.empty() || !resume_.empty();
        }
        // Otomatik bağlanma kapalı, pencere kapalı, istek yok: hiç aramaz (bağlı oturumlar sürer)
        const bool scan = autoConnect_ || watching_ || wantedAny;
        const LinkMode m = mode_;
        if (scan && m != LinkMode::Wifi) {
            if (adbPath.empty() && nowUs() >= nextAdbLookupUs) {
                adbPath = adb::find();
                if (!adbPath.empty()) logf("USB: adb %ls", adbPath.c_str());
                nextAdbLookupUs = nowUs() + 30 * kSecond;
            }
            if (adbPath.empty()) {
                if (!warnedNoAdb) logf("USB: adb bulunamadı (Android SDK platform-tools gerekli)");
                warnedNoAdb = true;
            } else {
                scanUsb(adbPath, usb.ok());
            }
        }
        if (stop_) break;
        if (scan && m != LinkMode::Usb) scanWifi();
        if (stop_) break;
        if (scan) startSessions(adbPath);
        publish();

        // Sıradaki iş: tarama zamanı ya da bekleyen cihazın yeniden denenmesi
        DWORD waitMs = INFINITE;
        if (scan) {
            const int64_t now = nowUs();
            int64_t next = now + 30 * kSecond;
            if (m != LinkMode::Wifi && !adbPath.empty()) next = std::min(next, nextUsbPollUs_);
            if (m != LinkMode::Usb) next = std::min(next, nextWifiUs_);
            {
                std::lock_guard<std::mutex> lock(m_);
                for (auto& [k, d] : devs_)
                    if (d->present && !d->busy && d->retryAtUs > now) next = std::min(next, d->retryAtUs);
            }
            waitMs = DWORD(std::clamp<int64_t>((next - now) / 1000, 50, 30000));
        }
        HANDLE hs[2] = {wake_, usb.event()};
        if (WaitForMultipleObjects(2, hs, FALSE, waitMs) == WAIT_OBJECT_0 + 1) {
            nextUsbPollUs_ = 0;
            usbBurstUntilUs_ = nowUs() + 10 * kSecond;
        }
    }

    // Kapanış: tüm oturumları kapat ve bekle
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(m_);
        for (auto& [k, d] : devs_) {
            d->cancel = true;
            if (d->conn) d->conn->close();
            if (d->worker.joinable()) workers.push_back(std::move(d->worker));
        }
    }
    for (auto& t : workers) t.join();
}

void Streamer::sessionWorker(Dev* d, std::wstring adbPath) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto finish = [&](int64_t retryUs, DeviceState st) {
        {
            std::lock_guard<std::mutex> lock(m_);
            d->info.state = st;
            d->info.gdiName.clear();
            d->retryAtUs = std::max(d->retryAtUs, nowUs() + retryUs);
            d->busy = false;
        }
        publish();
        SetEvent(wake_);
        CoUninitialize();
    };
    publish();  // "bağlanıyor"

    // Tarayıcı aynı anda günceller (Wi-Fi adresi, ad): kilit altında kopyala
    std::string ip, name;
    bool wasWaiting;
    unsigned local;
    {
        std::lock_guard<std::mutex> lock(m_);
        ip = d->ip;
        name = d->info.name;
        wasWaiting = d->info.state == DeviceState::WaitingApp;
        local = d->usb ? usbPorts_[d->serial] : 0;
    }
    std::string host;
    unsigned port = proto::kPort;
    if (d->usb) {
        if (!adb::prepareUsb(adbPath, d->serial, local, proto::kPort)) return finish(3 * kSecond, DeviceState::Found);
        {
            std::lock_guard<std::mutex> lock(m_);
            usbPorts_[d->serial] = local;
        }
        const adb::Launch l = adb::requestLaunch(adbPath, d->serial);
        if (l == adb::Launch::Declined) {
            if (!wasWaiting)
                logf("USB: %s bağlanmak istemiyor (\"otomatik aç\" kapalı ya da bağlantı cihazdan kesildi); "
                     "bağlanmak için cihazda EkranLink'i açın", name.c_str());
            return finish(4 * kSecond, DeviceState::WaitingApp);
        }
        if (l == adb::Launch::Failed) return finish(3 * kSecond, DeviceState::Found);
        if (l == adb::Launch::Started) {
            {
                std::lock_guard<std::mutex> lock(m_);
                d->info.state = DeviceState::Connecting;
            }
            publish();
            WaitForSingleObject(stopEvent_, 1500);  // cihaz sunucusunu açsın
        }
        host = "127.0.0.1";
        port = local;
    } else {
        host = ip;
    }
    bool cancelled;
    {
        std::lock_guard<std::mutex> lock(m_);
        cancelled = d->cancel || stop_;
        d->cancel = false;
    }
    if (cancelled) return finish(0, DeviceState::Found);

    const Result r = runSession(*d, host, port, adbPath);
    int64_t retry;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (r == Result::Ended) {
            // Kullanıcı PC'den kestiyse (held_) yeniden bağlanılmaz. Değilse bağlantı beklenmedik koptu (adb
            // sunucusu yeniden başladı, Wi-Fi kesildi...): otomatik bağlanma kapalı olsa da 1 dk yeniden denenir.
            // Cihazdan bilerek kesildiyse cihaz sunucusunu kapatır / USB'de açılmayı reddeder; denemeler boşa düşer.
            wanted_.erase(d->info.key);  // elle "Bağlan" isteği bir kerelik
            if (held_.count(d->info.key))
                resume_.erase(d->info.key);
            else
                resume_[d->info.key] = nowUs() + kWantUs;
            d->fails = 0;
            retry = kSecond;
        } else {
            d->fails = std::min(d->fails + 1, 5);
            retry = std::min<int64_t>(30 * kSecond, (2 * kSecond) << (d->fails - 1));
        }
    }
    finish(retry, DeviceState::Found);
}

Streamer::Result Streamer::runSession(Dev& d, const std::string& host, unsigned port, const std::wstring& adbPath) {
    Connection conn;
    proto::Hello hello;
    PairingHooks hooks;
    hooks.confirm = ev_.onPairRequest;
    std::string link, serial;
    const bool usb = d.usb;
    // USB kablosu (adb forward): görüntü şifresiz gidebilir. Ağ üzerinden adb (adb connect, kablosuz hata
    // ayıklama) yerel bağlantı sayılmaz.
    const bool localLink = usb && !adb::isNetworkSerial(d.serial);
    {
        std::lock_guard<std::mutex> lock(m_);
        link = d.info.link;
        serial = d.serial;
        // Eşleşmemiş cihaz için soru: kullanıcı "Bağlan" dediyse ya da cihaz USB'ye takıldıysa (son retten
        // sonraki 1 dk hariç). Ağdan kendiliğinden bulunan cihaz soru açtıramaz.
        hooks.mayPair = wanted_.count(d.info.key) || (localLink && nowUs() >= pairQuietUntilUs_);
        hooks.ignoreKey = d.authFailed;
    }
    if (!conn.connect(host, uint16_t(port), 2000)) return Result::Failed;
    bool shaken;
    try {
        shaken = conn.handshake(localLink, hooks, hello);
    } catch (const std::bad_alloc&) {
        logf("Bellek yetmedi: el sıkışma iptal");
        shaken = false;
    }
    if (!shaken) {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (conn.pairingRejected()) {
                d.retryAtUs = nowUs() + 60 * kSecond;  // 1 dk sorma
                pairQuietUntilUs_ = nowUs() + 60 * kSecond;
            }
            if (conn.needsPairing()) d.retryAtUs = nowUs() + 60 * kSecond;  // "Bağlan" beklenir
            if (conn.peerVersion()) d.retryAtUs = nowUs() + 30 * kSecond;
        }
        if (conn.authFailed()) {
            // Anahtarlar uyuşmuyor: kayıt silinmez; bir sonraki denemede (USB takılıysa ya da kullanıcı "Bağlan"
            // derse) iki ekranda kod onayıyla yeniden eşleştirilir
            bool first;
            std::string name;
            {
                std::lock_guard<std::mutex> lock(m_);
                first = !d.authFailed;
                d.authFailed = true;
                name = d.info.name;
            }
            if (first) notice(Notice::Repair, name, "Cihaz doğrulanamadı; yeniden eşleştirme gerekiyor");
        }
        if (conn.peerVersion()) {
            // Cihaz protokol sürümümüzü desteklemiyor: hangi tarafın güncelleneceğini söyle; güncellenene kadar
            // 30 sn'de bir dene (USB'de cihaz yeniden takılınca hemen)
            const bool tabletOld = conn.peerVersion() < proto::kVersion;
            notice(tabletOld ? Notice::TabletOutdated : Notice::PcOutdated, conn.peerAppVersion(),
                   tabletOld ? "Cihazdaki EkranLink eski" : "Bu bilgisayardaki EkranLink eski");
            logf("Sürüm uyuşmuyor: cihaz protokol %u (uygulama %s), PC %u", conn.peerVersion(),
                 conn.peerAppVersion().c_str(), proto::kVersion);
        }
        return Result::Failed;
    }
    if (!usb) config::rememberAddress(host);  // bir dahaki keşifte doğrudan sorulsun
    {
        std::lock_guard<std::mutex> lock(m_);
        if (d.cancel || stop_) return Result::Failed;
        d.conn = &conn;
        d.authFailed = false;
        d.info.deviceId = conn.deviceId();
        d.info.name = hello.model;
    }
    auto detach = [&] {
        std::lock_guard<std::mutex> lock(m_);
        d.conn = nullptr;
    };

    logf("Cihaz: %s, Android API %u, yüzey %ux%u, %u dpi, çözücü %s (%s, %s)", hello.model.c_str(), hello.sdk,
         hello.surfaceW, hello.surfaceH, hello.dpi, hello.decoder.c_str(), link.c_str(),
         conn.encrypted() ? "şifreli" : "şifresiz");

    const int W = (hello.surfaceW ? hello.surfaceW : hello.screenW) & ~1;
    const int H = (hello.surfaceH ? hello.surfaceH : hello.screenH) & ~1;

    // Oturum boyunca kalanlar: sanal monitör (ikinci ekranda), dokunma, sayaçlar, cihaz okuyucusu.
    // Görüntü boru hattı (yakalama, ölçekleyici, kodlayıcı) aşağıdaki döngüde gerektikçe yeniden kurulur;
    // bağlantı ve cihazdaki çözücü (akış boyutu W×H) hiç değişmez.
    VirtualDisplay vdisp;  // yakalama nesnelerinden önce tanımlı → oturum biterken monitör en son kaldırılır
    std::wstring target;
    bool only = false;     // tek ekran uygulandı (diğer monitörler kapalı)
    bool touchMouse = touchAsMouse_;
    TouchInjector touch(touchMouse ? TouchInjector::Mode::Mouse : TouchInjector::Mode::Touch);

    Stats stats;
    std::atomic<int> inFlight{0};
    std::atomic<int64_t> lastAckUs{nowUs()};
    HANDLE ackEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<int> wantRotation{-1};  // cihazın bildirdiği yön (-1: bildirmedi, dönüş kilitli)
    std::atomic<bool> keyRequested{false};  // cihaz çözücüsünü yeniden kurdu (ekranı kapanıp açıldı)
    std::atomic<bool> tabletVisible{true};  // cihaz uygulaması arka plandaysa görüntü üretilmez

    // Zaman damgası: gerçek kareler çift, itici kareler tek sayı (ayrı liste tutmadan ayırt etmek için).
    auto isPusher = [](int64_t pts) { return (pts & 1) != 0; };

    // Cihazdan gelenler: kare onayı, pong, dokunma
    std::thread reader([&] {
        uint8_t type;
        std::vector<uint8_t> payload;
        while (conn.readMessage(type, payload)) {
            proto::Reader r(payload.data(), payload.size());
            if (type == proto::kFrameAck) {
                const int64_t pts = int64_t(r.u64());
                const uint32_t decodeUs = r.u32();
                const int count = payload.size() >= 14 ? r.u16() : 1;  // toplu onay
                const int64_t now = nowUs();
                inFlight = std::max(0, inFlight.load() - count);
                lastAckUs = now;
                // Gecikmeyi sadece gerçek karelerle ölç: itici kareler bir sonraki değişime kadar
                // çözücüde bekleyebilir, ortalamayı bozarlar.
                if (!isPusher(pts)) {
                    stats.acks++;
                    stats.e2eSumUs += now - pts;
                    stats.decodeSumUs += decodeUs;
                }
                SetEvent(ackEvent);
            } else if (type == proto::kPong) {
                stats.rttUs = nowUs() - int64_t(r.u64());
            } else if (type == proto::kTouch) {
                const uint8_t action = r.u8(), id = r.u8();
                const int x = r.u16(), y = r.u16(), pressure = r.u16();
                if (r.ok()) touch.onTouch(action, id, x, y, pressure);
            } else if (type == proto::kKeyRequest) {
                keyRequested = true;
            } else if (type == proto::kVideoVisible) {
                const bool v = r.u8() != 0;
                if (r.ok() && v != tabletVisible.exchange(v))
                    logf(v ? "%s: uygulama öne geldi" : "%s: uygulama arka planda, görüntü duraklatıldı",
                         hello.model.c_str());
            } else if (type == proto::kOrientation) {
                const int deg = r.u16();
                if (r.ok() && deg % 90 == 0 && deg < 360) wantRotation = deg;
            }
        }
    });

    // Bağlı cihazın durumu listeye (arayüz) yansır
    auto report = [&](DisplayMode mode, const std::wstring& gdi) {
        {
            std::lock_guard<std::mutex> lock(m_);
            d.info.state = DeviceState::Connected;
            d.info.mode = mode;
            d.info.width = W;
            d.info.height = H;
            d.info.gdiName = gdi;
        }
        publish();
    };

    CursorState cursor;
    Waiter waiter;
    int appliedRotation = 0;
    int tabletApplied = -1;  // en son uygulanan cihaz bildirimi
    int lastFixed = -2;      // ayar değişimini fark etmek için (-2: monitör yeni)
    int appliedGen = -1;     // cihaza özel ayarların okunduğu update() sayısı (-1: monitör yeni)
    int expectW = 0, expectH = 0;  // beklenen çözünürlük (yatay yön): farklıysa Windows'tan değiştirildi
    bool tabletAwake = true;  // cihaz ekranı açık başlar
    // Kare hızı sınırı: cihazın ekranı 60 Hz; daha fazlası iki cihazı da boşuna yorar.
    const int64_t minFrameUs = 1000000 / std::max(1, opt_.fps);
    bool started = false;
    DisplayMode mode = DisplayMode::Extend;

    while (!stop_ && conn.alive()) {
        // --- Monitör: istenen moda göre ekle / kaldır / tek ekran ---
        const DisplayMode want = DisplayMode(display_.load());
        const int wantOutput = output_;
        const bool extend = want != DisplayMode::Mirror && W > 0 && H > 0 && VirtualDisplay::driverAvailable();
        if (extend && !vdisp.added()) {
            VirtualDisplaySpec s;
            s.width = W;
            s.height = H;
            s.refreshHz = 60;
            s.monitorGuid = VirtualDisplay::guidFor(conn.deviceId());
            s.name = "EkranLink";  // EDID monitör adı en çok 13 karakter
            s.serial = conn.deviceId().substr(0, 13);
            const double dpi = hello.dpi ? hello.dpi : 160;  // Windows ölçeklemeyi bu boyuta göre önerir
            s.widthMm = int(W * 25.4 / dpi + 0.5);
            s.heightMm = int(H * 25.4 / dpi + 0.5);
            if (!vdisp.add(s, target)) {
                target.clear();
                logf("İkinci ekran açılamadı; yansıtmaya geçiliyor");
            }
            lastFixed = -2;  // yeni monitör: yön ve çözünürlük yeniden uygulanır
            tabletApplied = -1;
            appliedGen = -1;
        } else if (!extend && vdisp.added()) {
            vdisp.remove();  // tek ekrandaysa önce diğer monitörler geri gelir
            target.clear();
        }
        if (!vdisp.added()) only = false;
        if (vdisp.added() && (want == DisplayMode::Only) != only) {
            if (want == DisplayMode::Only) vdisp.isolate();
            else vdisp.restoreLayout();
            only = want == DisplayMode::Only;
        }
        mode = target.empty() ? DisplayMode::Mirror : only ? DisplayMode::Only : DisplayMode::Extend;
        logMemory("el sıkışma sonrası");

        // --- Görüntü boru hattı ---
        followInputDesktop();  // ajan: kilit ekranındaysak oraya bağlan
        GpuDevice gpu;
        DesktopCapture cap;
        Scaler scaler;
        ComPtr<ID3D11Texture2D> pool[4];
        bool gpuOk = false;
        if (target.empty()) {
            gpuOk = createDeviceForOutput(wantOutput, gpu) && cap.init(gpu);
        } else {
            // Yeni monitör DXGI'de biraz geç görünebilir
            for (int i = 0; i < 30 && !stop_ && !gpuOk; i++) {
                gpuOk = createDeviceForOutputName(target, gpu) && cap.init(gpu);
                if (!gpuOk) Sleep(100);
            }
        }
        // Cihaza giden görüntünün yönü ve boyutu. Yakalanan görüntü panel yönündedir (orientation.h):
        //  - ikinci ekran: olduğu gibi gider (döndürme cihazın tutuluşundan; cihaz paneli de öyle döner)
        //  - yansıtma: PC monitörü dik kullanılıyorsa video işlemcide düzeltilir, cihazda dik görünür
        Orientation sent;
        int srcW = 0, srcH = 0;
        auto initScaler = [&] {
            const Orientation& o = cap.orientation();
            scaler = Scaler();
            if (!scaler.init(gpu.device.get(), gpu.context.get(), cap.desk(), cap.width(), cap.height(), W, H,
                             target.empty() ? o.rotation : 0))
                return false;
            if (scaler.rotated()) {
                sent = Orientation{0, o.deskW, o.deskH};
                srcW = o.deskW;
                srcH = o.deskH;
            } else {
                sent = o;
                srcW = cap.width();
                srcH = cap.height();
            }
            return true;
        };
        if (W <= 0 || H <= 0 || !gpuOk || !initScaler() || !makeNv12Pool(gpu.device.get(), W, H, pool)) break;

        logMemory("yakalama + ölçekleyici");
        RECT content = scaler.content();
        auto sendConfig = [&] {
            proto::Writer w;
            w.u16(uint16_t(W));
            w.u16(uint16_t(H));
            w.u8(1);  // H.264
            w.u16(uint16_t(srcW));
            w.u16(uint16_t(srcH));
            w.u16(uint16_t(content.left));
            w.u16(uint16_t(content.top));
            w.u16(uint16_t(content.right - content.left));
            w.u16(uint16_t(content.bottom - content.top));
            conn.send(proto::kConfig, w.buf);
        };
        sendConfig();
        touch.setMapping(gpu.desktopRect, content, srcW, srcH, sent);

        Encoder enc;
        auto onPacket = [&](EncodedPacket&& p) {
            uint8_t flags = p.key ? proto::kKeyFrame : 0;
            if (isPusher(p.ptsUs)) flags |= proto::kPusher;
            stats.bytes += int64_t(p.data.size());
            conn.sendVideo(p.ptsUs, flags, p.data.data(), p.data.size());
        };
        if (!enc.init(gpu.device.get(), gpu.vendorId, W, H, opt_.fps, opt_.kbps, onPacket)) break;
        logMemory("kodlayıcı");
        logf("Kodlayıcı: %s | akış %dx%d, içerik %ldx%ld, %d kbps", enc.name().c_str(), W, H,
             content.right - content.left, content.bottom - content.top, opt_.kbps);
        report(mode, target);  // bağlandı ya da görüntü modu değişti
        started = true;

        // Yeni monitörü Windows kendi kaydından döndürmüş / boyutlandırmış olabilir: gerçek değerden başla
        appliedRotation = target.empty() ? 0 : cap.orientation().rotation;
        if (!target.empty() && !vdisp.resolution(expectW, expectH)) expectW = expectH = 0;

        bool rebuild = false;
        bool dirty = true, forceKey = true;
        int pushersLeft = 0, slot = 0, lastSlot = 0;
        int64_t nextPusherUs = 0, lastPts = 0, nextStatsUs = nowUs() + 1000000;
        int64_t lastRealUs = 0;
        inFlight = 0;

        while (!stop_ && conn.alive()) {
            // --- Ayar değişiklikleri (bağlantı kopmaz) ---
            const DisplayMode nowWant = DisplayMode(display_.load());
            if (nowWant != want || (target.empty() && output_ != wantOutput)) {
                const bool bothVirtual = want != DisplayMode::Mirror && nowWant != DisplayMode::Mirror;
                if (!(bothVirtual && !target.empty())) {
                    rebuild = true;  // yansıtma ↔ ikinci ekran ya da başka PC ekranı: boru hattı yeniden kurulur
                    break;
                }
            }
            if (!target.empty()) {
                // İkinci ekran ↔ tek ekran: monitör yerinde kalır, yalnızca diğerleri kapanır / açılır
                const bool wantOnly = DisplayMode(display_.load()) == DisplayMode::Only;
                if (wantOnly != only) {
                    if (wantOnly) vdisp.isolate();
                    else vdisp.restoreLayout();
                    only = wantOnly;
                    mode = only ? DisplayMode::Only : DisplayMode::Extend;
                    report(mode, target);
                }
            }
            if (touchAsMouse_ != touchMouse) {
                touchMouse = touchAsMouse_;
                touch.setMode(touchMouse ? TouchInjector::Mode::Mouse : TouchInjector::Mode::Touch);
            }
            const bool lowLatency = lowLatency_;
            // İlk itici kare: düşük gecikmede kareler arasına (çözücü sırası hızlı akar), dengeli modda yalnızca
            // hareket durduğunda (bir kare süresi yeni görüntü gelmezse) — sürekli harekette çözücü 60/s çalışır.
            const int64_t firstPusherUs = lowLatency ? opt_.pusherMs * 1000 : minFrameUs + 2000;

            // Monitör yönü ve çözünürlüğü (yakalama kendiliğinden yeniden kurulur). Yansıtmada PC'nin gerçek
            // ekranına dokunulmaz.
            //  - yön: sabit seçiliyse o; otomatikte cihaz çevrilince cihazın yönü, bildirmiyorsa en son kullanılan
            //  - cihaz bildirimi yalnızca değişince uygulanır: Windows ayarlarından yapılan değişikliği ezmesin
            if (!target.empty()) {
                const int fixed = fixedRotation_;
                const int tablet = wantRotation;
                int rot = -1;
                if (fixed >= 0) rot = fixed;
                else if (tablet >= 0 && tablet != tabletApplied) rot = tabletApplied = tablet;
                else if (fixed != lastFixed) rot = tablet >= 0 ? tablet : config::lastRotation(conn.deviceId());  // otomatiğe geçildi
                lastFixed = fixed;
                if (rot >= 0 && rot != appliedRotation) {
                    appliedRotation = rot;
                    if (vdisp.setRotation(rot) && fixed < 0) config::saveLastRotation(conn.deviceId(), rot);
                }
                const int gen = settingsGen_;
                if (gen != appliedGen) {
                    // Bu cihaz için seçilmiş çözünürlük (yoksa cihazın kendisi = sürücünün önerdiği mod)
                    appliedGen = gen;
                    int rw = W, rh = H;
                    config::resolutionFor(conn.deviceId(), rw, rh);
                    int cw = 0, ch = 0;
                    if (vdisp.resolution(cw, ch) && (cw != rw || ch != rh) && vdisp.setResolution(rw, rh)) {
                        expectW = rw;
                        expectH = rh;
                    }
                }
            }

            // Uyku eşitleme: Windows ekranları kapatınca cihaz da ekranını kapatır; açınca uyanır
            if (displayPower_.on() != tabletAwake) {
                tabletAwake = !tabletAwake;
                proto::Writer w;
                w.u8(tabletAwake ? 1 : 0);
                conn.send(proto::kDisplayPower, w.buf);
                // Cihazda başka bir uygulama kullanılıyorsa ekranı kapatılmaz
                if (!tabletAwake && usb && !adbPath.empty() && tabletVisible) {
                    // USB: cihaz mesajı işleyip (uyku kilidi) hazırlansın, sonra ekranını kapat
                    std::thread([adbPath, serial, sdk = int(hello.sdk)] {
                        Sleep(300);
                        adb::screenOff(adbPath, serial, sdk);
                    }).detach();
                }
            }
            if (keyRequested.exchange(false)) {
                logf("%s: görüntü yeniden başladı (anahtar kare)", hello.model.c_str());
                inFlight = 0;  // kapalıyken gönderilenler hiç onaylanmayacak
                dirty = forceKey = true;
                cursor.shapeChanged = cursor.w > 0 && cursor.h > 0;  // cihaz imleci de unutmuş olabilir
                cursor.posChanged = true;
            }

            // Cihaz uygulaması arka planda: yakalama ve kodlama durur (iki cihaz da boşta); öne gelince
            // cihaz anahtar kare ister ve görüntü kaldığı yerden sürer.
            if (!tabletVisible) {
                WaitForSingleObject(ackEvent, 200);
                continue;
            }

            const bool busy = dirty || pushersLeft > 0;
            bool changed = false;
            auto res = cap.acquire(busy ? 0 : 100, changed, cursor);
            if (res == DesktopCapture::Result::Lost || res == DesktopCapture::Result::Error) {
                // UAC / kilit ekranı / çözünürlük, yön ya da yerleşim değişimi: yakalamayı yeniden kur.
                // Akış boyutu aynı kalır; yalnızca yerleşim ve eşleme tazelenir, bağlantı kopmaz.
                const int w0 = srcW, h0 = srcH, r0 = sent.rotation;
                // Ajan: kilit ekranı / UAC / oturum açma ayrı masaüstü; yakalama oraya geçerek sürer
                while (!stop_ && conn.alive()) {
                    followInputDesktop();
                    if (cap.init(gpu)) break;
                    Sleep(250);
                }
                if (stop_ || !conn.alive()) break;
                if (!initScaler()) break;
                content = scaler.content();
                if (srcW != w0 || srcH != h0) sendConfig();
                touch.setMapping(gpu.desktopRect, content, srcW, srcH, sent);
                // Yönü ya da çözünürlüğü biz değiştirmediysek kullanıcı Windows ekran ayarlarından değiştirdi:
                // hatırla (bir sonraki bağlantıda da öyle açılır)
                bool saved = false;
                const int now = cap.orientation().rotation;
                if (!target.empty() && now != appliedRotation) {
                    appliedRotation = now;
                    if (fixedRotation_ >= 0) {
                        fixedRotation_ = now;
                        lastFixed = now;
                        config::saveRotation(now);
                        saved = true;
                    } else {
                        config::saveLastRotation(conn.deviceId(), now);
                    }
                    logf("Yön Windows ayarlarından değişti: %d° (kaydedildi)", now);
                }
                int cw = 0, ch = 0;
                if (!target.empty() && vdisp.resolution(cw, ch) && (cw != expectW || ch != expectH)) {
                    expectW = cw;
                    expectH = ch;
                    config::saveResolution(conn.deviceId(), cw, ch);
                    saved = true;
                    logf("Çözünürlük Windows ayarlarından değişti: %dx%d (kaydedildi)", cw, ch);
                }
                if (saved && ev_.onSettingsChanged) ev_.onSettingsChanged();
                if (saved) report(mode, target);  // arayüz yeni çözünürlüğü / yönü okusun
                if (sent.rotation != r0 || srcW != w0) {
                    logf("Görüntü: %dx%d, yön %d°%s", srcW, srcH, cap.orientation().rotation,
                         scaler.rotated() ? " (görüntü düzeltildi)" : "");
                    // İmleç yeni yöne çevrilerek yeniden gönderilir (henüz resim gelmediyse yalnızca konum)
                    cursor.shapeChanged = cursor.w > 0 && cursor.h > 0;
                    cursor.posChanged = true;
                }
                dirty = forceKey = true;
                continue;
            }
            if (changed) {
                dirty = true;
                if (!lowLatency) pushersLeft = 0;  // dengeli: yeni görüntü geliyorsa itici kareye gerek yok
            }

            // İmleç masaüstü yönünde gelir; cihaza yakalanan görüntünün (panel) yönünde gönderilir.
            const Orientation& orient = sent;
            if (cursor.shapeChanged && (cursor.w <= 0 || cursor.h <= 0)) cursor.shapeChanged = false;
            if (cursor.shapeChanged) {
                int w = cursor.w, h = cursor.h, hx = cursor.hotX, hy = cursor.hotY;
                std::vector<uint32_t> px;
                const std::vector<uint32_t>* shape = &cursor.argb;
                if (orient.rotation != 0) {
                    px = cursor.argb;
                    orient.rotateImage(px, w, h, hx, hy);
                    shape = &px;
                }
                proto::Writer w8;
                w8.u16(uint16_t(w));
                w8.u16(uint16_t(h));
                w8.u16(uint16_t(hx));
                w8.u16(uint16_t(hy));
                for (uint32_t p : *shape) w8.u32(p);
                conn.send(proto::kCursorShape, w8.buf);
                cursor.shapeChanged = false;
                if (orient.rotation != 0) cursor.posChanged = true;  // sol üst köşe resmin boyutuna bağlı
            }
            if (cursor.posChanged) {
                const POINT p = orient.rectToImage(cursor.x, cursor.y, cursor.w, cursor.h);
                proto::Writer w;
                w.u16(uint16_t(int16_t(p.x)));
                w.u16(uint16_t(int16_t(p.y)));
                w.u8(cursor.visible ? 1 : 0);
                conn.send(proto::kCursorPos, w.buf);
                cursor.posChanged = false;
            }

            int64_t now = nowUs();
            if (inFlight >= opt_.maxInFlight && now - lastAckUs > 1000000) {
                inFlight = 0;  // onay kayboldu: sıfırla, anahtar kareyle toparla
                forceKey = true;
            }

            const bool room = enc.canAccept() && inFlight < opt_.maxInFlight;
            const bool rateLimited = dirty && now - lastRealUs < minFrameUs;
            // Düşük gecikmede, kare sınırı yüzünden beklerken araya itici kare girer (çözücü sırası hızlı akar)
            const bool pusherDue = (!dirty || (lowLatency && rateLimited)) && pushersLeft > 0 && now >= nextPusherUs;
            if (dirty && room && !rateLimited) {
                slot = (slot + 1) % 4;
                const int64_t pts = std::max(now & ~int64_t(1), lastPts + 2);
                if (scaler.process(pool[slot].get()) && enc.submit(pool[slot].get(), pts, forceKey)) {
                    inFlight++;
                    stats.frames++;
                    lastSlot = slot;
                    lastPts = pts;
                    lastRealUs = now;
                    forceKey = false;
                    dirty = false;
                    pushersLeft = opt_.pushers;
                    nextPusherUs = now + firstPusherUs;
                }
            } else if (pusherDue && room) {
                // Değişmemiş görüntüyü tekrar kodla: kodlayıcı "atla" blokları üretir, neredeyse sıfır bayt.
                const int64_t pts = lastPts + 1;  // tek sayı = itici
                if (enc.submit(pool[lastSlot].get(), pts, false)) {
                    inFlight++;
                    stats.pushers++;
                    lastPts = pts + 1;
                }
                pushersLeft--;
                nextPusherUs = now + opt_.pusherMs * 1000;
            }

            // Bekleme: meşgul döngü yok. Kare sınırındaysak sıradaki kare zamanına kadar uyu;
            // yer yoksa kodlayıcı/onay olayını bekle.
            if (rateLimited) {
                int64_t until = lastRealUs + minFrameUs;
                if (lowLatency && pushersLeft > 0) until = std::min(until, nextPusherUs);
                waiter.waitUs(until - nowUs());
            } else if ((dirty || pusherDue) && !room) {
                HANDLE hs[2] = {enc.wakeEvent(), ackEvent};
                WaitForMultipleObjects(2, hs, FALSE, 5);
            } else if (!dirty && pushersLeft > 0) {
                waiter.waitUs(nextPusherUs - nowUs());
            }

            now = nowUs();
            if (now >= nextStatsUs) {
                proto::Writer w;
                w.u64(uint64_t(now));
                conn.send(proto::kPing, w.buf);

                const int acks = stats.acks.exchange(0);
                const int64_t e2e = stats.e2eSumUs.exchange(0), dec = stats.decodeSumUs.exchange(0);
                char line[256];
                std::snprintf(line, sizeof line,
                              "%3d kare +%3d itici  %6.2f Mbit/s  RTT %5.1f ms  PC→çizim %5.1f ms  çözme %5.1f ms",
                              stats.frames.exchange(0), stats.pushers.exchange(0), stats.bytes.exchange(0) * 8 / 1e6,
                              stats.rttUs / 1000.0, acks ? e2e / 1000.0 / acks : 0.0, acks ? dec / 1000.0 / acks : 0.0);
                if (ev_.onStats) ev_.onStats(d.info.key, line);
                logMemory("yayın sürerken");
                nextStatsUs = now + 1000000;
            }
        }

        enc.shutdown();
        if (!rebuild) break;
        logf("Görüntü yeniden kuruluyor (bağlantı sürüyor)");
        // İmleç yeni görüntüde yeniden gönderilsin
        cursor.shapeChanged = cursor.w > 0 && cursor.h > 0;
        cursor.posChanged = true;
    }

    conn.close();
    if (reader.joinable()) reader.join();
    CloseHandle(ackEvent);
    detach();
    if (!started) return Result::Failed;
    logf("Bağlantı kapandı: %s (%s)", hello.model.c_str(), link.c_str());
    return Result::Ended;
}

}  // namespace ekran
