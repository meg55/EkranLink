// Tabletten gelen dokunmaları Windows'a iletir.
//  - Dokunmatik ekran (varsayılan): gerçek çoklu dokunma (InjectTouchInput, en fazla 10 parmak). Windows'un
//    kendi hareketleri çalışır: kaydırma, iki parmakla yakınlaştırma, basılı tutunca sağ tık, dokunmatik klavye.
//  - Fare: tek parmak = sol tık (dokunmayı iyi desteklemeyen eski programlar için).
#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "orientation.h"

namespace ekran {

class TouchInjector {
public:
    enum class Mode { Touch, Mouse };

    explicit TouchInjector(Mode mode = Mode::Touch);
    ~TouchInjector();

    // desktop: yakalanan ekranın sanal masaüstündeki yeri; content: görüntünün akıştaki yeri;
    // imageW×imageH: yakalanan görüntü (panel yönü), orient: masaüstüne göre dönüşü
    void setMapping(RECT desktop, RECT content, int imageW, int imageH, Orientation orient);
    void onTouch(uint8_t action, uint8_t pointerId, int x, int y, int pressure);

private:
    static constexpr int kMaxContacts = 10;
    struct Contact {
        bool active = false;
        POINT pt{};
        UINT32 pressure = 512;
    };

    POINT toScreen(int x, int y) const;
    // Tüm etkin parmakları tek çerçevede gönderir; `changed` parmağına `changedFlags` uygulanır.
    void injectLocked(int changed, UINT32 changedFlags);
    void keepAlive();
    void mouse(uint8_t action, uint8_t pointerId, POINT pt);

    Mode mode_;
    RECT desktop_{};
    RECT content_{};
    int imageW_ = 1, imageH_ = 1;
    Orientation orient_;
    Contact contacts_[kMaxContacts];
    int activeCount_ = 0;
    bool mouseDown_ = false;
    bool warned_ = false;

    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread keeper_;
};

}  // namespace ekran
