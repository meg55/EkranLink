// Hat: DXGI yakalama → GPU video işlemci (ölçek + NV12) → donanım H.264 → TCP → tablet.
// Görüntü hiç CPU belleğine inmez. Ekran değişmiyorsa kodlama da gönderim de yapılmaz.

#include "streamer.h"

#include <winsock2.h>
#include <psapi.h>

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

}  // namespace

Streamer::Streamer(Options opt, StreamerEvents ev)
    : opt_(std::move(opt)), ev_(std::move(ev)), mode_(opt_.mode), wake_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
    fixedRotation_ = opt_.rotation;
}

Streamer::~Streamer() { CloseHandle(wake_); }

void Streamer::stop() {
    stop_ = true;
    SetEvent(wake_);
    std::lock_guard<std::mutex> lock(connMutex_);
    if (conn_) conn_->close();
}

void Streamer::setMode(LinkMode m) {
    mode_ = m;
    SetEvent(wake_);
    std::lock_guard<std::mutex> lock(connMutex_);
    if (conn_) conn_->close();
}

void Streamer::notice(Notice n, const std::string& arg, const char* logText) {
    // Bağlantı birkaç saniyede bir yeniden denenir: aynı uyarı 10 dakikada bir
    const int64_t now = nowUs();
    if (int(n) == lastNotice_ && now < lastNoticeUs_ + 600'000'000) return;
    lastNotice_ = int(n);
    lastNoticeUs_ = now;
    logf("%s", logText);
    if (ev_.onNotice) ev_.onNotice(n, arg);
}

void Streamer::sleepInterruptible(int ms) { WaitForSingleObject(wake_, DWORD(ms)); }

void Streamer::connectNow() {
    manualUntilUs_ = nowUs() + 30'000'000;  // 30 sn içinde bulunan tablete bir kez bağlan
    SetEvent(wake_);
}

void Streamer::run() {
    if (!displayPower_.start()) logf("Ekran güç bildirimi alınamıyor; uyku eşitleme kapalı");
    UsbWatch usb;
    std::wstring adbPath;
    std::string prepared;  // uygulaması bulunup port yönlendirilmiş aygıt (her takılışta bir kez)
    int64_t nextAdbLookupUs = 0, nextUsbPollUs = 0, usbBurstUntilUs = 0;
    bool warnedNoAdb = false, declinedLogged = false, manualLogged = false;

    while (!stop_) {
        // Otomatik bağlanma kapalı: yalnızca "Şimdi bağlan" ile
        if (!opt_.autoConnect && nowUs() >= manualUntilUs_) {
            if (!manualLogged) logf("Otomatik bağlanma kapalı; bağlanmak için menüden \"Şimdi bağlan\"");
            manualLogged = true;
            WaitForSingleObject(wake_, INFINITE);
            continue;
        }
        manualLogged = false;
        const LinkMode m = mode_;
        bool tried = false, connected = false;

        if (m != LinkMode::Wifi) {
            if (adbPath.empty() && nowUs() >= nextAdbLookupUs) {
                adbPath = adb::find();
                if (!adbPath.empty()) logf("USB: adb %ls", adbPath.c_str());
                nextAdbLookupUs = nowUs() + 30'000'000;
            }
            if (adbPath.empty()) {
                if (!warnedNoAdb) logf("USB: adb bulunamadı (Android SDK platform-tools gerekli)");
                warnedNoAdb = true;
            } else if (nowUs() >= nextUsbPollUs) {
                // Tablet takılı değilken adb çalıştırılmaz: Windows'un takılma bildirimi beklenir
                // (bildirim yoksa 3 sn'de bir); takıldıktan sonraki birkaç saniye adb'nin görmesi beklenir.
                const int64_t now = nowUs();
                nextUsbPollUs = now + (now < usbBurstUntilUs ? 1'000'000 : usb.ok() ? 30'000'000 : 3'000'000);
                const std::string dev = adb::serial(adbPath);
                if (dev.empty()) {
                    prepared.clear();
                    declinedLogged = false;
                } else if (dev != prepared && adb::appState(adbPath) == adb::App::Missing) {
                    notice(Notice::AppMissing, {}, "USB: tablette EKRAN uygulaması yok; APK tablete ayrıca kurulmalı");
                    nextUsbPollUs = nowUs() + 10'000'000;  // kurulmasını bekle
                } else if (!adb::prepareUsb(adbPath, proto::kPort)) {
                    nextUsbPollUs = nowUs() + 3'000'000;  // ör. tablette "USB hata ayıklamaya izin ver" bekleniyor
                } else {
                    prepared = dev;
                    const adb::Launch l = adb::requestLaunch(adbPath);
                    if (l == adb::Launch::Declined) {
                        if (!declinedLogged) logf("USB: tablet bağlanmak istemiyor (\"otomatik aç\" kapalı ya da bağlantı tabletten kesildi); bağlanmak için tablette EKRAN'ı açın");
                        declinedLogged = true;
                        nextUsbPollUs = nowUs() + 4'000'000;  // takılıyken uygulamanın açılmasını bekle
                    } else if (l != adb::Launch::Failed) {
                        declinedLogged = false;
                        if (l == adb::Launch::Started) sleepInterruptible(1500);  // tablet sunucusunu açsın
                        tried = true;
                        connected = runSession("127.0.0.1", "USB", adbPath);
                        // bağlantı bitince hemen yeniden bak; sürüm uyuşmuyorsa bir süre bekle
                        nextUsbPollUs = incompatibleHost_ == "127.0.0.1" ? incompatibleUntilUs_ : 0;
                    }
                }
            }
        }
        if (stop_) break;

        if (!tried && m != LinkMode::Usb) {
            std::string ip = opt_.ip;
            if (ip.empty()) {
                const int64_t now = nowUs();
                for (const auto& dev : discoverDevices(1000, config::knownAddresses())) {
                    auto it = rejectedUntil_.find(dev.id);
                    if (it != rejectedUntil_.end() && it->second > now) continue;  // az önce reddedildi
                    ip = dev.ip;
                    break;
                }
            }
            if (ip == incompatibleHost_ && nowUs() < incompatibleUntilUs_) ip.clear();
            if (!ip.empty()) {
                tried = true;
                connected = runSession(ip, "Wi-Fi " + ip);
            }
        }
        if (connected && !opt_.autoConnect) manualUntilUs_ = 0;  // elle bağlanma bir kerelik
        if (tried) {
            if (!stop_) sleepInterruptible(1000);
            continue;
        }
        // Kimse yok. Wi-Fi'de 3 sn'de bir sorulur (UDP, çok hafif); yalnız USB'de takılma beklenir.
        const int64_t untilUsb = std::max<int64_t>(0, (nextUsbPollUs - nowUs()) / 1000);
        const DWORD waitMs = m == LinkMode::Usb ? DWORD(std::min<int64_t>(untilUsb, 30'000)) : 3000;
        HANDLE hs[2] = {wake_, usb.event()};
        if (WaitForMultipleObjects(2, hs, FALSE, waitMs) == WAIT_OBJECT_0 + 1) {
            nextUsbPollUs = 0;
            usbBurstUntilUs = nowUs() + 10'000'000;
        }
    }
}

bool Streamer::runSession(const std::string& host, const std::string& link, const std::wstring& adbPath) {
    Connection conn;
    proto::Hello hello;
    PairingHooks hooks;
    hooks.confirm = ev_.onPairRequest;
    const bool usb = link == "USB";  // adb forward: kablo + adb yetkisi = güvenilir bağlantı
    if (!conn.connect(host, proto::kPort, 2000)) return false;
    if (!conn.handshake(usb, hooks, hello)) {
        if (conn.pairingRejected()) rejectedUntil_[conn.deviceId()] = nowUs() + 60'000'000;  // 1 dk sorma
        if (conn.peerVersion()) {
            // Tablet protokol sürümümüzü desteklemiyor: hangi tarafın güncelleneceğini söyle;
            // güncellenene kadar 30 sn'de bir dene (USB'de tablet yeniden takılınca hemen)
            incompatibleHost_ = host;
            incompatibleUntilUs_ = nowUs() + 30'000'000;
            const bool tabletOld = conn.peerVersion() < proto::kVersion;
            notice(tabletOld ? Notice::TabletOutdated : Notice::PcOutdated, conn.peerAppVersion(),
                   tabletOld ? "Tabletteki EKRAN eski" : "Bu bilgisayardaki EKRAN eski");
            logf("Sürüm uyuşmuyor: tablet protokol %u (uygulama %s), PC %u", conn.peerVersion(),
                 conn.peerAppVersion().c_str(), proto::kVersion);
        }
        return false;
    }
    if (!usb) config::rememberAddress(host);  // bir dahaki keşifte doğrudan sorulsun
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        conn_ = &conn;
    }
    auto detach = [&] {
        std::lock_guard<std::mutex> lock(connMutex_);
        conn_ = nullptr;
    };
    if (stop_) {
        detach();
        return false;
    }

    logf("Tablet: %s, Android API %u, yüzey %ux%u, %u dpi, çözücü %s (%s, %s)", hello.model.c_str(), hello.sdk,
         hello.surfaceW, hello.surfaceH, hello.dpi, hello.decoder.c_str(), link.c_str(),
         conn.encrypted() ? "şifreli" : "şifresiz");

    const int W = (hello.surfaceW ? hello.surfaceW : hello.screenW) & ~1;
    const int H = (hello.surfaceH ? hello.surfaceH : hello.screenH) & ~1;

    // İkinci ekran: tabletin tam çözünürlüğünde sanal monitör (yakalama nesnelerinden önce tanımlı →
    // en son yok edilir, yani oturum biterken monitör en son kaldırılır).
    VirtualDisplay vdisp;
    std::wstring target;
    if (opt_.extend && W > 0 && H > 0 && VirtualDisplay::driverAvailable()) {
        VirtualDisplaySpec s;
        s.width = W;
        s.height = H;
        s.refreshHz = 60;
        s.monitorGuid = VirtualDisplay::guidFor(conn.deviceId());
        s.name = "EKRAN Tablet";
        s.serial = conn.deviceId().substr(0, 13);
        const double dpi = hello.dpi ? hello.dpi : 160;  // Windows ölçeklemeyi bu boyuta göre önerir
        s.widthMm = int(W * 25.4 / dpi + 0.5);
        s.heightMm = int(H * 25.4 / dpi + 0.5);
        if (!vdisp.add(s, target)) {
            target.clear();
            logf("İkinci ekran açılamadı; yansıtmaya geçiliyor");
        }
    }
    const bool only = !target.empty() && opt_.onlyTablet && vdisp.isolate();
    logMemory("el sıkışma sonrası");

    followInputDesktop();  // ajan: kilit ekranındaysak oraya bağlan
    GpuDevice gpu;
    DesktopCapture cap;
    Scaler scaler;
    ComPtr<ID3D11Texture2D> pool[4];
    bool gpuOk = false;
    if (target.empty()) {
        gpuOk = createDeviceForOutput(opt_.output, gpu) && cap.init(gpu);
    } else {
        // Yeni monitör DXGI'de biraz geç görünebilir
        for (int i = 0; i < 30 && !stop_ && !gpuOk; i++) {
            gpuOk = createDeviceForOutputName(target, gpu) && cap.init(gpu);
            if (!gpuOk) Sleep(100);
        }
    }
    // Tablete giden görüntünün yönü ve boyutu. Yakalanan görüntü panel yönündedir (orientation.h):
    //  - ikinci ekran: olduğu gibi gider (döndürme tabletin tutuluşundan; tablet paneli de öyle döner)
    //  - yansıtma: PC monitörü dik kullanılıyorsa video işlemcide düzeltilir, tablette dik görünür
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
    if (W <= 0 || H <= 0 || !gpuOk || !initScaler() || !makeNv12Pool(gpu.device.get(), W, H, pool)) {
        detach();
        return false;
    }

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

    TouchInjector touch(opt_.touchAsMouse ? TouchInjector::Mode::Mouse : TouchInjector::Mode::Touch);
    touch.setMapping(gpu.desktopRect, content, srcW, srcH, sent);

    Stats stats;
    std::atomic<int> inFlight{0};
    std::atomic<int64_t> lastAckUs{nowUs()};
    HANDLE ackEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<int> wantRotation{-1};  // tabletin bildirdiği yön (-1: bildirmedi, dönüş kilitli)
    std::atomic<bool> keyRequested{false};  // tablet çözücüsünü yeniden kurdu (ekranı kapanıp açıldı)
    std::atomic<bool> tabletVisible{true};  // tablet uygulaması arka plandaysa görüntü üretilmez

    // Zaman damgası: gerçek kareler çift, itici kareler tek sayı (ayrı liste tutmadan ayırt etmek için).
    auto isPusher = [](int64_t pts) { return (pts & 1) != 0; };

    Encoder enc;
    auto onPacket = [&](EncodedPacket&& p) {
        uint8_t flags = p.key ? proto::kKeyFrame : 0;
        if (isPusher(p.ptsUs)) flags |= proto::kPusher;
        stats.bytes += int64_t(p.data.size());
        conn.sendVideo(p.ptsUs, flags, p.data.data(), p.data.size());
    };
    if (!enc.init(gpu.device.get(), gpu.vendorId, W, H, opt_.fps, opt_.kbps, onPacket)) {
        CloseHandle(ackEvent);
        detach();
        return false;
    }
    logMemory("kodlayıcı");
    logf("Kodlayıcı: %s | akış %dx%d, içerik %ldx%ld, %d kbps", enc.name().c_str(), W, H,
         content.right - content.left, content.bottom - content.top, opt_.kbps);
    const DisplayMode dmode = target.empty() ? DisplayMode::Mirror : only ? DisplayMode::Only : DisplayMode::Extend;
    if (ev_.onState) ev_.onState(true, hello.model, link, dmode);

    // Tabletten gelenler: kare onayı, pong, dokunma
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
                    logf(v ? "Tablet uygulaması öne geldi" : "Tablet uygulaması arka planda: görüntü duraklatıldı");
            } else if (type == proto::kOrientation) {
                const int deg = r.u16();
                if (r.ok() && deg % 90 == 0 && deg < 360) wantRotation = deg;
            }
        }
    });

    CursorState cursor;
    Waiter waiter;
    // Monitör yönü. Yeni sanal monitörü Windows kendi kaydından döndürmüş olabilir: gerçek yönden başla.
    int appliedRotation = target.empty() ? 0 : cap.orientation().rotation;
    int tabletApplied = -1;  // en son uygulanan tablet bildirimi
    int lastFixed = -2;      // ayar değişimini fark etmek için (-2: oturum başı)
    bool tabletAwake = true;  // tablet ekranı açık başlar
    bool dirty = true, forceKey = true;
    int pushersLeft = 0, slot = 0, lastSlot = 0;
    int64_t nextPusherUs = 0, lastPts = 0, nextStatsUs = nowUs() + 1000000;
    // Kare hızı sınırı: tabletin ekranı 60 Hz; daha fazlası iki cihazı da boşuna yorar.
    const int64_t minFrameUs = 1000000 / std::max(1, opt_.fps);
    int64_t lastRealUs = 0;
    // İlk itici kare: düşük gecikmede kareler arasına (çözücü sırası hızlı akar), dengeli modda yalnızca
    // hareket durduğunda (bir kare süresi yeni görüntü gelmezse) — sürekli harekette çözücü 60/s çalışır.
    const int64_t firstPusherUs = opt_.lowLatency ? opt_.pusherMs * 1000 : minFrameUs + 2000;

    while (!stop_ && conn.alive()) {
        // Monitör yönü (yakalama kendiliğinden yeniden kurulur). Yansıtmada PC'nin gerçek ekranına dokunulmaz.
        //  - sabit yön seçiliyse o
        //  - otomatikte tablet çevrilince tabletin yönü; tablet bildirmiyorsa en son kullanılan yön
        // Tablet bildirimi yalnızca değişince uygulanır: Windows ayarlarından yapılan değişikliği ezmesin.
        if (!target.empty()) {
            const int fixed = fixedRotation_;
            const int tablet = wantRotation;
            int want = -1;
            if (fixed >= 0) want = fixed;
            else if (tablet >= 0 && tablet != tabletApplied) want = tabletApplied = tablet;
            else if (fixed != lastFixed) want = tablet >= 0 ? tablet : config::lastRotation();  // otomatiğe geçildi
            lastFixed = fixed;
            if (want >= 0 && want != appliedRotation) {
                appliedRotation = want;
                if (vdisp.setRotation(want) && fixed < 0) config::saveLastRotation(want);
            }
        }

        // Uyku eşitleme: Windows ekranları kapatınca tablet de ekranını kapatır; açınca uyanır
        if (displayPower_.on() != tabletAwake) {
            tabletAwake = !tabletAwake;
            proto::Writer w;
            w.u8(tabletAwake ? 1 : 0);
            conn.send(proto::kDisplayPower, w.buf);
            // Tablette başka bir uygulama kullanılıyorsa ekranı kapatılmaz
            if (!tabletAwake && !adbPath.empty() && tabletVisible) {
                // USB: tablet mesajı işleyip (uyku kilidi) hazırlansın, sonra ekranını kapat
                std::thread([adbPath, sdk = int(hello.sdk)] {
                    Sleep(300);
                    adb::screenOff(adbPath, sdk);
                }).detach();
            }
        }
        if (keyRequested.exchange(false)) {
            logf("Tablet görüntüyü yeniden başlattı (anahtar kare)");
            inFlight = 0;  // kapalıyken gönderilenler hiç onaylanmayacak
            dirty = forceKey = true;
            cursor.shapeChanged = cursor.w > 0 && cursor.h > 0;  // tablet imleci de unutmuş olabilir
            cursor.posChanged = true;
        }

        // Tablet uygulaması arka planda: yakalama ve kodlama durur (iki cihaz da boşta); öne gelince
        // tablet anahtar kare ister ve görüntü kaldığı yerden sürer.
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
            // Yönü biz değiştirmediysek kullanıcı Windows ekran ayarlarından değiştirdi: hatırla
            const int now = cap.orientation().rotation;
            if (!target.empty() && now != appliedRotation) {
                appliedRotation = now;
                if (fixedRotation_ >= 0) {
                    fixedRotation_ = now;
                    lastFixed = now;
                    config::saveRotation(now);
                    if (ev_.onRotationChanged) ev_.onRotationChanged(now);
                } else {
                    config::saveLastRotation(now);
                }
                logf("Yön Windows ayarlarından değişti: %d° (kaydedildi)", now);
            }
            if (sent.rotation != r0 || srcW != w0) {
                logf("Yön: %d°%s", cap.orientation().rotation, scaler.rotated() ? " (görüntü düzeltildi)" : "");
                // İmleç yeni yöne çevrilerek yeniden gönderilir (henüz resim gelmediyse yalnızca konum)
                cursor.shapeChanged = cursor.w > 0 && cursor.h > 0;
                cursor.posChanged = true;
            }
            dirty = forceKey = true;
            continue;
        }
        if (changed) {
            dirty = true;
            if (!opt_.lowLatency) pushersLeft = 0;  // dengeli: yeni görüntü geliyorsa itici kareye gerek yok
        }

        // İmleç masaüstü yönünde gelir; tablete yakalanan görüntünün (panel) yönünde gönderilir.
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
        const bool pusherDue = (!dirty || (opt_.lowLatency && rateLimited)) && pushersLeft > 0 && now >= nextPusherUs;
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
            if (opt_.lowLatency && pushersLeft > 0) until = std::min(until, nextPusherUs);
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
            if (ev_.onStats) ev_.onStats(line);
            logMemory("yayın sürerken");
            nextStatsUs = now + 1000000;
        }
    }

    enc.shutdown();
    conn.close();
    if (reader.joinable()) reader.join();
    CloseHandle(ackEvent);
    detach();
    logf("Bağlantı kapandı (%s)", link.c_str());
    if (ev_.onState) ev_.onState(false, hello.model, link, dmode);
    return true;
}

}  // namespace ekran
