// Döndürülmüş ekranlarda masaüstü koordinatı ↔ yakalanan görüntü (panel yönü) dönüşümü.
//
// Desktop Duplication, döndürülmüş bir ekranın görüntüsünü panelin kendi yönünde verir. Tablet ekranı
// da hep yatay kaldığından bu görüntü olduğu gibi gönderilir; yalnızca imleç ve dokunma çevrilir.
// Ölçüldü: DMDO_90'da masaüstünün sol üstü görüntünün sol altına düşer (tablet saat yönünde çevrilmiş).
#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace ekran {

struct Orientation {
    int rotation = 0;         // 0, 90, 180, 270 (DMDO_* × 90)
    int deskW = 0, deskH = 0; // masaüstü (döndürülmüş) boyutu

    // Masaüstünde (x, y, w, h) dikdörtgeninin görüntüdeki sol üstü
    POINT rectToImage(int x, int y, int w, int h) const {
        switch (rotation) {
            case 90: return {y, deskW - x - w};
            case 180: return {deskW - x - w, deskH - y - h};
            case 270: return {deskH - y - h, x};
            default: return {x, y};
        }
    }

    // Görüntü pikseli → masaüstü pikseli (çıkışın sol üstüne göre)
    POINT imageToDesktop(int ix, int iy) const {
        switch (rotation) {
            case 90: return {deskW - 1 - iy, ix};
            case 180: return {deskW - 1 - ix, deskH - 1 - iy};
            case 270: return {iy, deskH - 1 - ix};
            default: return {ix, iy};
        }
    }

    // w×h ARGB resmi görüntü yönüne çevirir; w, h ve sıcak nokta güncellenir.
    void rotateImage(std::vector<uint32_t>& px, int& w, int& h, int& hotX, int& hotY) const {
        if (rotation == 0 || px.size() != size_t(w) * h) return;
        std::vector<uint32_t> out(px.size());
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                const uint32_t v = px[size_t(j) * w + i];
                if (rotation == 90) out[size_t(w - 1 - i) * h + j] = v;
                else if (rotation == 180) out[size_t(h - 1 - j) * w + (w - 1 - i)] = v;
                else out[size_t(i) * h + (h - 1 - j)] = v;
            }
        const int hx = hotX, hy = hotY;
        if (rotation == 90) hotX = hy, hotY = w - 1 - hx;
        else if (rotation == 180) hotX = w - 1 - hx, hotY = h - 1 - hy;
        else hotX = h - 1 - hy, hotY = hx;
        if (rotation != 180) {
            const int t = w;
            w = h;
            h = t;
        }
        px = std::move(out);
    }
};

}  // namespace ekran
