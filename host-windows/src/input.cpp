#include "input.h"

#include <algorithm>
#include <chrono>

#include "desktop.h"
#include "log.h"
#include "protocol.h"

namespace ekran {

namespace {

// InitializeTouchInjection süreç başına bir kez
bool touchInjectionReady() {
    static const bool ok = [] {
        if (InitializeTouchInjection(10, TOUCH_FEEDBACK_DEFAULT)) return true;
        logf("Dokunmatik giriş açılamadı (hata %lu); fare moduna geçiliyor", GetLastError());
        return false;
    }();
    return ok;
}

constexpr UINT32 kDown = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
constexpr UINT32 kUpdate = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
constexpr UINT32 kUp = POINTER_FLAG_UP;
constexpr UINT32 kCancel = POINTER_FLAG_UP | POINTER_FLAG_CANCELED;

}  // namespace

TouchInjector::TouchInjector(Mode mode) : mode_(mode) {
    if (mode_ == Mode::Touch && !touchInjectionReady()) mode_ = Mode::Mouse;
    if (mode_ == Mode::Touch) {
        keeper_ = std::thread([this] { keepAlive(); });
        keeperStarted_ = true;
    }
}

void TouchInjector::setMode(Mode mode) {
    std::lock_guard<std::mutex> lock(m_);
    if (mode == Mode::Touch && !touchInjectionReady()) mode = Mode::Mouse;
    if (mode == mode_) return;
    // Yarım kalan dokunma ya da basılı sol tuş Windows'ta takılı kalmasın
    for (int i = 0; i < kMaxContacts; i++)
        if (contacts_[i].active) injectLocked(i, kCancel);
    if (mouseDown_) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof in);
    }
    dropContactsLocked();
    mode_ = mode;
    if (mode_ == Mode::Touch && !keeperStarted_) {
        keeper_ = std::thread([this] { keepAlive(); });  // kilidi bıraktıktan sonra çalışmaya başlar
        keeperStarted_ = true;
    }
    logf("Dokunma: %s", mode_ == Mode::Touch ? "dokunmatik ekran" : "fare");
}

TouchInjector::~TouchInjector() {
    {
        std::lock_guard<std::mutex> lock(m_);
        for (int i = 0; i < kMaxContacts; i++)
            if (contacts_[i].active) injectLocked(i, kCancel);  // yarım kalan dokunma Windows'ta takılı kalmasın
        stop_ = true;
    }
    cv_.notify_all();
    if (keeper_.joinable()) keeper_.join();
}

void TouchInjector::setMapping(RECT desktop, RECT content, int imageW, int imageH, Orientation orient) {
    std::lock_guard<std::mutex> lock(m_);
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
    std::lock_guard<std::mutex> lock(m_);
    if (followInputDesktop()) dropContactsLocked();  // ajan: kilit ekranına / UAC'ye de dokunulabilsin
    const POINT pt = toScreen(x, y);
    if (mode_ == Mode::Mouse) {
        mouse(action, pointerId, pt);
        return;
    }
    if (pointerId >= kMaxContacts) return;
    Contact& c = contacts_[pointerId];

    switch (action) {
        case proto::kTouchDown:
            if (c.active) injectLocked(pointerId, kUp);  // kaçırılmış "bırak" varsa önce kapat
            c.active = true;
            c.pt = pt;
            c.pressure = UINT32(std::clamp(pressure, 1, 1024));
            activeCount_++;
            injectLocked(pointerId, kDown);
            cv_.notify_all();  // tutma zamanlayıcısını başlat
            break;
        case proto::kTouchMove:
            if (!c.active) return;
            c.pt = pt;
            c.pressure = UINT32(std::clamp(pressure, 1, 1024));
            injectLocked(pointerId, kUpdate);
            break;
        case proto::kTouchUp:
            if (!c.active) return;
            c.pt = pt;
            injectLocked(pointerId, kUp);
            break;
        case proto::kTouchCancel:
            // Tablet dokunmayı iptal etti (ör. sistem hareketi): tüm parmakları iptal et
            for (int i = 0; i < kMaxContacts; i++)
                if (contacts_[i].active) injectLocked(i, kCancel);
            break;
    }
}

void TouchInjector::injectLocked(int changed, UINT32 changedFlags) {
    POINTER_TOUCH_INFO frame[kMaxContacts];
    UINT32 n = 0;
    for (int i = 0; i < kMaxContacts; i++) {
        const Contact& c = contacts_[i];
        if (!c.active) continue;
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
    if (n > 0 && !InjectTouchInput(n, frame) && !warned_) {
        logf("Dokunma iletilemedi (hata %lu)", GetLastError());
        warned_ = true;
    }
    // Kalkan/iptal edilen parmağı listeden çıkar
    if (changed >= 0 && (changedFlags & POINTER_FLAG_UP) && contacts_[changed].active) {
        contacts_[changed].active = false;
        activeCount_--;
    }
}

// Giriş masaüstü değişti (ör. kilit perdesi kaydırılınca şifre ekranı): o anda ekrandaki parmaklar yeni
// masaüstünde hiç basılmadı, hareketlerinin kalanı geçersiz olur. Bırakılmış say; sonraki dokunma temiz başlar.
void TouchInjector::dropContactsLocked() {
    for (auto& c : contacts_) c.active = false;
    activeCount_ = 0;
    mouseDown_ = false;
}

// Parmak hareketsiz dururken de Windows'a düzenli güncelleme gerekir (gerçek dokunmatik ekranlar da
// sürekli rapor eder; aksi halde basılı tutma / sağ tık algılanmaz). Yalnızca dokunulurken çalışır.
void TouchInjector::keepAlive() {
    std::unique_lock<std::mutex> lock(m_);
    while (!stop_) {
        if (activeCount_ == 0) {
            cv_.wait(lock, [this] { return stop_ || activeCount_ > 0; });  // boşta: hiç uyanmaz
            continue;
        }
        cv_.wait_for(lock, std::chrono::milliseconds(100));
        if (!stop_ && activeCount_ > 0) {
            if (followInputDesktop()) dropContactsLocked();
            else injectLocked(-1, 0);
        }
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
