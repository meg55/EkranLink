// Cihazdan gelen dokunmaları Windows'a iletir.
//  - Dokunmatik ekran (varsayılan): gerçek çoklu dokunma (InjectTouchInput, en fazla 10 parmak). Windows'un
//    kendi hareketleri çalışır: kaydırma, iki parmakla yakınlaştırma, basılı tutunca sağ tık, dokunmatik klavye.
//  - Fare: tek parmak = sol tık (dokunmayı iyi desteklemeyen eski programlar için).
// Birden çok cihaz bağlıyken parmaklar süreç genelinde tek tabloda tutulur: Windows'a giden her çerçeve tüm
// cihazlardaki parmakları içerir (yoksa bir cihazın dokunması diğerininkini yarıda keserdi).
#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>

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
    // Bağlantı sürerken mod değiştir (basılı parmak / fare tuşu önce bırakılır)
    void setMode(Mode mode);

private:
    POINT toScreen(int x, int y) const;
    void cancelOwnLocked();
    void mouse(uint8_t action, uint8_t pointerId, POINT pt);

    Mode mode_;
    RECT desktop_{};
    RECT content_{};
    int imageW_ = 1, imageH_ = 1;
    Orientation orient_;
    bool mouseDown_ = false;
};

}  // namespace ekran
