// Pencerelerin renkleri (Windows'un uygulama teması: açık / koyu). Ayar penceresi ve eşleştirme sorusu ortak kullanır.
#pragma once

#include <windows.h>
#include <d2d1.h>

#include <cstdint>

namespace ekran::theme {

struct Theme {
    bool dark = false;
    D2D1_COLOR_F bg, card, border, text, sub, control, controlHot, accent, accentHot, onAccent, ok, idle, warn, warnBg;
};

inline D2D1_COLOR_F rgb(uint32_t c, float a = 1.f) {
    return D2D1::ColorF(float((c >> 16) & 0xFF) / 255.f, float((c >> 8) & 0xFF) / 255.f, float(c & 0xFF) / 255.f, a);
}

// Bu sürecin kullanıcısının ayarı (hizmet ajanı SYSTEM'dir: onun için tepsi kullanıcınınkini "UiDark" olarak yazar)
inline bool systemDark() {
    DWORD v = 1, size = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v == 0;
}

inline Theme makeTheme(bool dark = systemDark()) {
    Theme t;
    t.dark = dark;
    if (t.dark) {
        t.bg = rgb(0x202020);
        t.card = rgb(0x2B2B2B);
        t.border = rgb(0x3A3A3A);
        t.text = rgb(0xF2F2F2);
        t.sub = rgb(0xA7A7A7);
        t.control = rgb(0x383838);
        t.controlHot = rgb(0x444444);
        t.accent = rgb(0x3D8BFF);
        t.accentHot = rgb(0x5A9DFF);
        t.onAccent = rgb(0xFFFFFF);
        t.ok = rgb(0x3CCB7F);
        t.idle = rgb(0xE0A43A);
        t.warn = rgb(0xFF9A76);
        t.warnBg = rgb(0x3A2A22);
    } else {
        t.bg = rgb(0xF3F3F3);
        t.card = rgb(0xFFFFFF);
        t.border = rgb(0xE3E3E3);
        t.text = rgb(0x1B1B1B);
        t.sub = rgb(0x5E5E5E);
        t.control = rgb(0xEDEDED);
        t.controlHot = rgb(0xE2E2E2);
        t.accent = rgb(0x0F6CE0);
        t.accentHot = rgb(0x2B7DE8);
        t.onAccent = rgb(0xFFFFFF);
        t.ok = rgb(0x1E9E5A);
        t.idle = rgb(0xC98A1B);
        t.warn = rgb(0xB5401F);
        t.warnBg = rgb(0xFDEDE7);
    }
    return t;
}

}  // namespace ekran::theme
