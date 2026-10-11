#include "input.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <thread>

#include "desktop.h"
#include "log.h"
#include "protocol.h"

namespace ekran {

namespace {

constexpr int kMaxContacts = 10;

// InitializeTouchInjection süreç başına bir kez
bool touchInjectionReady() {
    static const bool ok = [] {
        if (InitializeTouchInjection(kMaxContacts, TOUCH_FEEDBACK_DEFAULT)) return true;
        logf("Dokunmatik giriş açılamadı (hata %lu); fare moduna geçiliyor", GetLastError());
        return false;
    }();
    return ok;
}

constexpr UINT32 kDown = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
constexpr UINT32 kUpdate = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
constexpr UINT32 kUp = POINTER_FLAG_UP;
constexpr UINT32 kCancel = POINTER_FLAG_UP | POINTER_FLAG_CANCELED;

// Tüm cihazların parmakları. Windows'a giden işaretçi kimliği = yuva sırası.
struct Hub {
    struct Slot {
        const TouchInjector* owner = nullptr;
        int local = -1;  // cihazdaki parmak kimliği
        POINT pt{};
        UINT32 pressure = 512;
    };
    std::mutex m;
    std::condition_variable cv;
    Slot slots[kMaxContacts];
    int active = 0;
    bool keeperStarted = false;
    bool warned = false;

    int find(const TouchInjector* o, int local) const {
        for (int i = 0; i < kMaxContacts; i++)
            if (slots[i].owner == o && slots[i].local == local) return i;
        return -1;
    }
    int freeSlot() const {
        for (int i = 0; i < kMaxContacts; i++)
            if (!slots[i].owner) return i;
        return -1;
    }

    // Tüm etkin parmakları tek çerçevede gönderir; `changed` yuvasına `changedFlags` uygulanır.
    void injectLocked(int changed, UINT32 changedFlags) {
        POINTER_TOUCH_INFO frame[kMaxContacts];
        UINT32 n = 0;
        for (int i = 0; i < kMaxContacts; i++) {
            const Slot& c = slots[i];
            if (!c.owner) continue;
            POINTER_TOUCH_INFO& t = frame[n++];
            t = {};
            t.pointerInfo.pointerType = PT_TOUCH;
            t.pointerInfo.pointerId = UINT32(i);
            t.pointerInfo.ptPixelLocation = c.pt;
            t.pointerInfo.pointerFlags = i == changed ? changedFlags : kUpdate;
            t.touchFlags = TOUCH_FLAG_NONE;
            t.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
            t.orientation = 90;
            t.pressure = c.pressure;
            t.rcContact = {c.pt.x - 2, c.pt.y - 2, c.pt.x + 2, c.pt.y + 2};
        }
        if (n > 0 && !InjectTouchInput(n, frame) && !warned) {
            logf("Dokunma iletilemedi (hata %lu)", GetLastError());
            warned = true;
        }
        // Kalkan/iptal edilen parmağı tablodan çıkar
        if (changed >= 0 && (changedFlags & POINTER_FLAG_UP) && slots[changed].owner) {
            slots[changed] = Slot{};
            active--;
        }
    }

    // Giriş masaüstü değişti (ör. kilit perdesi kaydırılınca şifre ekranı): o anda ekrandaki parmaklar yeni
    // masaüstünde hiç basılmadı, hareketlerinin kalanı geçersiz olur. Bırakılmış say; sonraki dokunma temiz başlar.
    void dropAllLocked() {
        for (auto& s : slots) s = Slot{};
        active = 0;
    }

    // Parmak hareketsiz dururken de Windows'a düzenli güncelleme gerekir (gerçek dokunmatik ekranlar da
    // sürekli rapor eder; aksi halde basılı tutma / sağ tık algılanmaz). Yalnızca dokunulurken çalışır.
    void keepAlive() {
        std::unique_lock<std::mutex> lock(m);
        for (;;) {
            if (active == 0) {
                cv.wait(lock, [this] { return active > 0; });  // boşta: hiç uyanmaz
                continue;
            }
            cv.wait_for(lock, std::chrono::milliseconds(100));
            if (active > 0) {
                if (followInputDesktop()) dropAllLocked();
                else injectLocked(-1, 0);
            }
        }
    }

    void ensureKeeperLocked() {
        if (keeperStarted) return;
        keeperStarted = true;
        std::thread([this] { keepAlive(); }).detach();  // süreç boyunca; boştayken uyur
    }
};

Hub& hub() {
    static Hub* h = new Hub;  // hiç yok edilmez: arka plandaki iş parçacığı süreç sonuna kadar kullanır
    return *h;
}

}  // namespace

TouchInjector::TouchInjector(Mode mode) : mode_(mode) {
    if (mode_ == Mode::Touch && !touchInjectionReady()) mode_ = Mode::Mouse;
}

void TouchInjector::cancelOwnLocked() {
    Hub& h = hub();
    for (int i = 0; i < kMaxContacts; i++)
        if (h.slots[i].owner == this) h.injectLocked(i, kCancel);
}

void TouchInjector::setMode(Mode mode) {
    std::lock_guard<std::mutex> lock(hub().m);
    if (mode == Mode::Touch && !touchInjectionReady()) mode = Mode::Mouse;
    if (mode == mode_) return;
    // Yarım kalan dokunma ya da basılı sol tuş Windows'ta takılı kalmasın
    cancelOwnLocked();
    if (mouseDown_) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof in);
        mouseDown_ = false;
    }
    mode_ = mode;
    logf("Dokunma: %s", mode_ == Mode::Touch ? "dokunmatik ekran" : "fare");
}

TouchInjector::~TouchInjector() {
    std::lock_guard<std::mutex> lock(hub().m);
    cancelOwnLocked();  // yarım kalan dokunma Windows'ta takılı kalmasın
}

void TouchInjector::setMapping(RECT desktop, RECT content, int imageW, int imageH, Orientation orient) {
    std::lock_guard<std::mutex> lock(hub().m);
    desktop_ = desktop;
    content_ = content;
    imageW_ = std::max(1, imageW);
    imageH_ = std::max(1, imageH);
    orient_ = orient;
}

POINT TouchInjector::toScreen(int x, int y) const {
    const int cw = std::max<int>(1, content_.right - content_.left);
    const int ch = std::max<int>(1, content_.bottom - content_.top);
    // Akış pikseli → yakalanan görüntü pikseli (siyah bantlara dokunulursa kenara yapışır) → masaüstü pikseli
    const int ix = int(int64_t(std::clamp(x - int(content_.left), 0, cw - 1)) * imageW_ / cw);
    const int iy = int(int64_t(std::clamp(y - int(content_.top), 0, ch - 1)) * imageH_ / ch);
    POINT p = orient_.imageToDesktop(ix, iy);
    p.x += desktop_.left;
    p.y += desktop_.top;
    return p;
}

void TouchInjector::onTouch(uint8_t action, uint8_t pointerId, int x, int y, int pressure) {
    Hub& h = hub();
    std::lock_guard<std::mutex> lock(h.m);
    if (followInputDesktop()) {  // ajan: kilit ekranına / UAC'ye de dokunulabilsin
        h.dropAllLocked();
        mouseDown_ = false;
    }
    const POINT pt = toScreen(x, y);
    if (mode_ == Mode::Mouse) {
        mouse(action, pointerId, pt);
        return;
    }
    int s = h.find(this, pointerId);
    switch (action) {
        case proto::kTouchDown:
            if (s >= 0) h.injectLocked(s, kUp);  // kaçırılmış "bırak" varsa önce kapat
            s = h.freeSlot();
            if (s < 0) return;  // 10 parmak dolu (tüm cihazlarda)
            h.slots[s].owner = this;
            h.slots[s].local = pointerId;
            h.slots[s].pt = pt;
            h.slots[s].pressure = UINT32(std::clamp(pressure, 1, 1024));
            h.active++;
            h.injectLocked(s, kDown);
            h.ensureKeeperLocked();
            h.cv.notify_all();  // tutma zamanlayıcısını başlat
            break;
        case proto::kTouchMove:
            if (s < 0) return;
            h.slots[s].pt = pt;
            h.slots[s].pressure = UINT32(std::clamp(pressure, 1, 1024));
            h.injectLocked(s, kUpdate);
            break;
        case proto::kTouchUp:
            if (s < 0) return;
            h.slots[s].pt = pt;
            h.injectLocked(s, kUp);
            break;
        case proto::kTouchCancel:
            // Cihaz dokunmayı iptal etti (ör. sistem hareketi): bu cihazın tüm parmaklarını iptal et
            cancelOwnLocked();
            break;
    }
}

void TouchInjector::mouse(uint8_t action, uint8_t pointerId, POINT pt) {
    if (pointerId != 0) return;  // fare modunda yalnızca ilk parmak
    DWORD extra = 0;
    switch (action) {
        case proto::kTouchDown:
            extra = MOUSEEVENTF_LEFTDOWN;
            mouseDown_ = true;
            break;
        case proto::kTouchMove:
            if (!mouseDown_) return;
            break;
        case proto::kTouchUp:
        case proto::kTouchCancel:
            if (!mouseDown_) return;
            extra = MOUSEEVENTF_LEFTUP;
            mouseDown_ = false;
            break;
        default:
            return;
    }
    // Masaüstü pikseli → SendInput'un 0..65535 sanal masaüstü ölçeği
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = std::max(2, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int vh = std::max(2, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = LONG(int64_t(pt.x - vx) * 65535 / (vw - 1));
    in.mi.dy = LONG(int64_t(pt.y - vy) * 65535 / (vh - 1));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | extra;
    SendInput(1, &in, sizeof in);
}

}  // namespace ekran
