#include "ui.h"

#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <functional>

#include "com.h"
#include "config.h"
#include "i18n.h"

namespace ekran::ui {

namespace {

using i18n::W;

// --- Tema (Windows'un uygulama teması: açık / koyu) ---

struct Theme {
    bool dark = false;
    D2D1_COLOR_F bg, card, border, text, sub, control, controlHot, accent, accentHot, onAccent, ok, idle, warn, warnBg;
};

D2D1_COLOR_F rgb(uint32_t c, float a = 1.f) {
    return D2D1::ColorF(float((c >> 16) & 0xFF) / 255.f, float((c >> 8) & 0xFF) / 255.f, float(c & 0xFF) / 255.f, a);
}

bool systemDark() {
    DWORD v = 1, size = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v == 0;
}

Theme makeTheme() {
    Theme t;
    t.dark = systemDark();
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

// --- Ölçüler (DIP). Her sayfa varsayılan boyutta kaydırmadan sığar; pencere küçültülürse sayfa kayar. ---

constexpr float kWidth = 840, kHeight = 620, kMinWidth = 700, kMinHeight = 480;
constexpr float kSide = 230;  // sol menü
constexpr float kMargin = 24, kCardPad = 16, kRowGap = 14, kControlH = 32, kRadius = 8;

enum Page { kStatus, kConnection, kDisplay, kInput, kGeneral, kPageCount };

struct Hit {
    D2D1_RECT_F r;
    std::function<void()> fn;
    bool fixed;  // sol menü: kaydırmayla oynamaz
};

bool inside(const D2D1_RECT_F& r, D2D1_POINT_2F p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

class Window {
public:
    Window(Host* host, HICON icon) : host_(host), icon_(icon) {}
    ~Window() { release(); }

    bool create();
    HWND hwnd() const { return hwnd_; }
    void refresh() {
        model_ = host_->model();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    LRESULT proc(UINT msg, WPARAM w, LPARAM l);

private:
    bool ensureTarget();
    void release();
    void paint();
    void applyTitleBarTheme();
    void setPage(int p) {
        page_ = (p + kPageCount) % kPageCount;
        scroll_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // Yerleşim + çizim: her boyamada baştan (anlık mod). draw_ false iken yalnızca ölçülür.
    // scroll_ yalnızca sayfa içeriğine uygulanır (fixed_ = sol menü çizilirken 0 kabul edilir).
    float off() const { return fixed_ ? 0.f : scroll_; }
    float textHeight(const std::wstring& s, IDWriteTextFormat* f, float w);
    float drawText(const std::wstring& s, IDWriteTextFormat* f, float x, float y, float w, D2D1_COLOR_F c);
    float textWidth(const std::wstring& s, IDWriteTextFormat* f);
    void fillRound(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c);
    void strokeRound(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c, float width = 1.f);
    void dot(float cx, float cy, float r, D2D1_COLOR_F c);
    D2D1_POINT_2F mouseFor() const { return D2D1::Point2F(mouse_.x, mouse_.y + off()); }
    bool hot(const D2D1_RECT_F& r) const { return mouseIn_ && inside(r, mouseFor()); }
    void addHit(const D2D1_RECT_F& r, std::function<void()> fn) {
        if (draw_) hits_.push_back({r, std::move(fn), fixed_});
    }

    void sidebar(float height);
    void pageTitle(const wchar_t* text);
    void card(const std::function<void()>& body, bool warn = false);
    void divider();
    void label(const wchar_t* text);
    void hint(const std::wstring& text, D2D1_COLOR_F color);
    void keyValue(const wchar_t* key, const std::wstring& value);
    void segmented(const std::vector<std::wstring>& options, int sel, const std::vector<bool>& enabled,
                   std::function<void(int)> onSelect);
    void dropdown(const std::vector<std::wstring>& items, int sel, bool enabled, std::function<void(int)> onSelect);
    void toggleRow(const wchar_t* text, const wchar_t* hintText, bool value, bool enabled, std::function<void()> onToggle);
    struct Button {
        std::wstring text;
        bool primary;
        bool enabled;
        std::function<void()> fn;
    };
    void buttons(const std::vector<Button>& list);

    void statusPage();
    void connectionPage();
    void displayPage();
    void inputPage();
    void generalPage();

    std::wstring resolutionText(const Resolution& r) const;

    Host* host_;
    HICON icon_;
    HWND hwnd_ = nullptr;
    Model model_;
    Theme theme_;
    float dpi_ = 96;
    int page_ = kStatus;

    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID2D1HwndRenderTarget> rt_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> fApp_, fTitle_, fSection_, fLabel_, fBody_, fHint_, fControl_, fNav_, fIcon_;

    bool draw_ = true, fixed_ = false;
    float x_ = 0, y_ = 0, w_ = 0;  // geçerli içerik sütunu (içerik koordinatı)
    float scroll_ = 0, contentH_ = 0, viewH_ = 0;
    std::vector<Hit> hits_;
    D2D1_POINT_2F mouse_{};  // istemci koordinatı (DIP)
    bool mouseIn_ = false;
    int pressed_ = -1;
};

Window* g_window = nullptr;

bool Window::create() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                 reinterpret_cast<void**>(d2d_.put()))))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.put()))))
        return false;
    auto fmt = [&](ComPtr<IDWriteTextFormat>& f, const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size) {
        dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                  L"", f.put());
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    };
    fmt(fApp_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 22);
    fmt(fTitle_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 26);
    fmt(fSection_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 15);
    fmt(fLabel_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 14);
    fmt(fBody_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 13);
    fmt(fHint_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12);
    fmt(fControl_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 13);
    fmt(fNav_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 14);
    fControl_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fControl_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fNav_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    // Menü simgeleri: Windows 11'in simge yazı tipi, yoksa Windows 10'unki
    {
        ComPtr<IDWriteFontCollection> fonts;
        UINT32 idx = 0;
        BOOL found = FALSE;
        dwrite_->GetSystemFontCollection(fonts.put(), FALSE);
        if (fonts) fonts->FindFamilyName(L"Segoe Fluent Icons", &idx, &found);
        fmt(fIcon_, found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_NORMAL, 16);
        fIcon_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    theme_ = makeTheme();
    model_ = host_->model();

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_NCCREATE) {
                auto* self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
                self->hwnd_ = h;
                SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
            return self ? self->proc(m, w, l) : DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"EkranAyarlar";
        RegisterClassExW(&wc);
        registered = true;
    }

    // İmlecin bulunduğu ekranın ortasında, çalışma alanına sığacak boyutta
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    if (auto f = reinterpret_cast<GetDpiForMonitorFn>(
            reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor"))))
        f(mon, 0, &dx, &dy);
    const float s = float(dx) / 96.f;
    const RECT& wa = mi.rcWork;
    const int w = std::min(int(kWidth * s), int(wa.right - wa.left));
    const int h = std::min(int(kHeight * s), int(wa.bottom - wa.top));
    hwnd_ = CreateWindowExW(0, L"EkranAyarlar", L"EKRAN", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                            wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                            nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_));
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_));
    dpi_ = float(GetDpiForWindow(hwnd_));
    applyTitleBarTheme();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return true;
}

void Window::applyTitleBarTheme() {
    const BOOL dark = theme_.dark;
    DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
}

void Window::release() {
    brush_.reset();
    rt_.reset();
}

bool Window::ensureTarget() {
    if (rt_) return true;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = props.dpiY = dpi_;
    if (FAILED(d2d_->CreateHwndRenderTarget(
            props, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(UINT32(rc.right), UINT32(rc.bottom))),
            rt_.put())))
        return false;
    rt_->CreateSolidColorBrush(theme_.text, brush_.put());
    return true;
}

// --- Çizim yardımcıları ---

float Window::textHeight(const std::wstring& s, IDWriteTextFormat* f, float w) {
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), UINT32(s.size()), f, std::max(w, 1.f), 10000.f, l.put()))) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.height;
}

float Window::drawText(const std::wstring& s, IDWriteTextFormat* f, float x, float y, float w, D2D1_COLOR_F c) {
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), UINT32(s.size()), f, std::max(w, 1.f), 10000.f, l.put()))) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    if (draw_) {
        brush_->SetColor(c);
        rt_->DrawTextLayout(D2D1::Point2F(x, y - off()), l.get(), brush_.get());
    }
    return m.height;
}

float Window::textWidth(const std::wstring& s, IDWriteTextFormat* f) {
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), UINT32(s.size()), f, 10000.f, 100.f, l.put()))) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void Window::fillRound(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c) {
    if (!draw_) return;
    brush_->SetColor(c);
    rt_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(r.left, r.top - off(), r.right, r.bottom - off()), radius, radius), brush_.get());
}

void Window::strokeRound(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c, float width) {
    if (!draw_) return;
    brush_->SetColor(c);
    const float h = width / 2;
    rt_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(r.left + h, r.top + h - off(), r.right - h, r.bottom - h - off()), radius, radius),
        brush_.get(), width);
}

void Window::dot(float cx, float cy, float r, D2D1_COLOR_F c) {
    if (!draw_) return;
    brush_->SetColor(c);
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy - off()), r, r), brush_.get());
}

// --- Bileşenler ---

void Window::sidebar(float height) {
    const Theme& t = theme_;
    const Model& m = model_;
    fixed_ = true;
    const float x = 16, w = kSide - 28;
    float y = 22;
    y += drawText(L"EKRAN", fApp_.get(), x + 6, y, w, t.text) + 6;
    // Durum: nokta + kısa metin + cihaz adı
    dot(x + 11, y + 9, 5, m.connected ? t.ok : t.idle);
    y += drawText(m.connected ? W(i18n::Connected) : W(i18n::Waiting), fBody_.get(), x + 24, y, w - 24, t.text) + 1;
    if (m.connected) y += drawText(m.device, fHint_.get(), x + 24, y, w - 24, t.sub);
    y = std::max(y + 22, 118.f);

    struct Nav {
        int page;
        const wchar_t* icon;
        i18n::Str text;
    };
    // Segoe Fluent Icons / MDL2: Info, Connect, TVMonitor, TouchPointer, Settings
    const Nav nav[] = {{kStatus, L"", i18n::NavStatus},
                       {kConnection, L"", i18n::SecConnection},
                       {kDisplay, L"", i18n::SecDisplay},
                       {kInput, L"", i18n::SecInput},
                       {kGeneral, L"", i18n::SecGeneral}};
    for (const Nav& n : nav) {
        const D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + 40);
        const bool sel = page_ == n.page;
        if (sel) fillRound(r, 6, t.control);
        else if (hot(r)) fillRound(r, 6, t.controlHot);
        if (sel) fillRound(D2D1::RectF(x, y + 12, x + 3, y + 28), 1.5f, t.accent);
        drawText(n.icon, fIcon_.get(), x + 14, y + 11, 24, t.text);
        const float th = textHeight(W(n.text), fNav_.get(), w - 50);
        drawText(W(n.text), fNav_.get(), x + 46, y + (40 - th) / 2, w - 50, t.text);
        addHit(r, [this, p = n.page] { setPage(p); });
        y += 44;
    }
    drawText(i18n::F(i18n::Version, config::kVersion), fHint_.get(), x + 6, height - 34, w, t.sub);
    fixed_ = false;
}

void Window::pageTitle(const wchar_t* text) { y_ += drawText(text, fTitle_.get(), x_, y_, w_, theme_.text) + 18; }

void Window::card(const std::function<void()>& body, bool warn) {
    const float x0 = x_, w0 = w_, top = y_;
    x_ += kCardPad;
    w_ -= 2 * kCardPad;

    // Önce ölç, sonra kartı ve içeriği çiz
    const bool wasDrawing = draw_;
    draw_ = false;
    y_ = top + kCardPad;
    body();
    const float bottom = y_ + kCardPad - kRowGap;
    draw_ = wasDrawing;

    const D2D1_RECT_F r = D2D1::RectF(x0, top, x0 + w0, bottom);
    fillRound(r, kRadius, warn ? theme_.warnBg : theme_.card);
    strokeRound(r, kRadius, theme_.border);
    y_ = top + kCardPad;
    body();

    x_ = x0;
    w_ = w0;
    y_ = bottom + 16;
}

void Window::divider() {
    if (draw_) {
        brush_->SetColor(theme_.border);
        // Piksel ortasına hizala: yoksa 1 piksellik çizgi iki satıra yayılıp soluklaşır
        const float px = dpi_ / 96.f;
        const float y = (std::floor((y_ - kRowGap / 2 - off()) * px) + 0.5f) / px;
        rt_->DrawLine(D2D1::Point2F(x_, y), D2D1::Point2F(x_ + w_, y), brush_.get(), 1.f);
    }
}

void Window::label(const wchar_t* text) { y_ += drawText(text, fLabel_.get(), x_, y_, w_, theme_.text) + 6; }

void Window::hint(const std::wstring& text, D2D1_COLOR_F color) {
    y_ += drawText(text, fHint_.get(), x_, y_, w_, color) + kRowGap;
}

void Window::keyValue(const wchar_t* key, const std::wstring& value) {
    const float kw = 150;
    drawText(key, fBody_.get(), x_, y_, kw, theme_.sub);
    y_ += drawText(value, fBody_.get(), x_ + kw, y_, w_ - kw, theme_.text) + 8;
}

void Window::segmented(const std::vector<std::wstring>& options, int sel, const std::vector<bool>& enabled,
                       std::function<void(int)> onSelect) {
    const Theme& t = theme_;
    const D2D1_RECT_F box = D2D1::RectF(x_, y_, x_ + w_, y_ + kControlH);
    fillRound(box, 6, t.control);
    const float seg = w_ / float(options.size());
    for (size_t i = 0; i < options.size(); i++) {
        const D2D1_RECT_F r = D2D1::RectF(x_ + seg * float(i) + 2, y_ + 2, x_ + seg * float(i + 1) - 2, y_ + kControlH - 2);
        const bool on = int(i) == sel, ok = enabled.empty() || enabled[i];
        if (on) fillRound(r, 5, ok && hot(r) ? t.accentHot : t.accent);
        else if (ok && hot(r)) fillRound(r, 5, t.controlHot);
        D2D1_COLOR_F c = on ? t.onAccent : t.text;
        if (!ok) c.a = 0.4f;
        const float th = textHeight(options[i], fControl_.get(), r.right - r.left - 8);
        drawText(options[i], fControl_.get(), r.left + 4, r.top + (r.bottom - r.top - th) / 2, r.right - r.left - 8, c);
        if (ok && !on) addHit(r, [onSelect, i] { onSelect(int(i)); });
    }
    y_ += kControlH + 8;
}

void Window::dropdown(const std::vector<std::wstring>& items, int sel, bool enabled, std::function<void(int)> onSelect) {
    const Theme& t = theme_;
    const D2D1_RECT_F box = D2D1::RectF(x_, y_, x_ + w_, y_ + kControlH);
    fillRound(box, 6, enabled && hot(box) ? t.controlHot : t.control);
    D2D1_COLOR_F c = t.text;
    if (!enabled) c.a = 0.45f;
    const std::wstring text = sel >= 0 && sel < int(items.size()) ? items[size_t(sel)] : L"";
    const float th = textHeight(text, fBody_.get(), w_ - 40);
    drawText(text, fBody_.get(), x_ + 12, y_ + (kControlH - th) / 2, w_ - 40, c);
    if (draw_) {  // aşağı ok
        brush_->SetColor(c);
        const float cx = x_ + w_ - 18, cy = y_ + kControlH / 2 - off();
        rt_->DrawLine(D2D1::Point2F(cx - 5, cy - 2.5f), D2D1::Point2F(cx, cy + 2.5f), brush_.get(), 1.5f);
        rt_->DrawLine(D2D1::Point2F(cx, cy + 2.5f), D2D1::Point2F(cx + 5, cy - 2.5f), brush_.get(), 1.5f);
    }
    if (enabled && !items.empty()) {
        const float bx = x_, by = y_ + kControlH;
        addHit(box, [this, items, sel, onSelect, bx, by] {
            HMENU m = CreatePopupMenu();
            for (size_t i = 0; i < items.size(); i++)
                AppendMenuW(m, MF_STRING | (int(i) == sel ? MF_CHECKED : 0), i + 1, items[i].c_str());
            const float s = dpi_ / 96.f;
            POINT p{LONG(bx * s), LONG((by - scroll_) * s)};
            ClientToScreen(hwnd_, &p);
            const int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd_, nullptr);
            DestroyMenu(m);
            if (r > 0 && r - 1 != sel) onSelect(r - 1);
        });
    }
    y_ += kControlH + 8;
}

void Window::toggleRow(const wchar_t* text, const wchar_t* hintText, bool value, bool enabled,
                       std::function<void()> onToggle) {
    const Theme& t = theme_;
    const float sw = 40, sh = 20, textW = w_ - sw - 16;
    const float top = y_;
    float h = drawText(text, fLabel_.get(), x_, y_, textW, enabled ? t.text : t.sub);
    if (hintText) h += 3 + drawText(hintText, fHint_.get(), x_, y_ + h + 3, textW, t.sub);
    const float rowH = std::max(h, sh);
    const D2D1_RECT_F s = D2D1::RectF(x_ + w_ - sw, top + 1, x_ + w_, top + 1 + sh);
    const D2D1_RECT_F row = D2D1::RectF(x_, top - 4, x_ + w_, top + rowH + 4);
    D2D1_COLOR_F fill = value ? (enabled && hot(row) ? t.accentHot : t.accent) : (enabled && hot(row) ? t.controlHot : t.control);
    if (!enabled) fill.a = 0.5f;
    fillRound(s, sh / 2, fill);
    if (!value) strokeRound(s, sh / 2, t.sub, 1.f);
    D2D1_COLOR_F knob = value ? t.onAccent : t.sub;
    if (!enabled) knob.a = 0.5f;
    dot(value ? s.right - 10 : s.left + 10, (s.top + s.bottom) / 2, value ? 7.f : 6.f, knob);
    if (enabled) addHit(row, onToggle);
    y_ = top + rowH + kRowGap;
}

void Window::buttons(const std::vector<Button>& list) {
    const Theme& t = theme_;
    float x = x_;
    for (const Button& b : list) {
        const float bw = textWidth(b.text, fControl_.get()) + 28;
        if (x > x_ && x + bw > x_ + w_) {  // sığmazsa alt satıra
            x = x_;
            y_ += kControlH + 8;
        }
        const D2D1_RECT_F r = D2D1::RectF(x, y_, x + bw, y_ + kControlH);
        const bool h = b.enabled && hot(r);
        D2D1_COLOR_F bg = b.primary ? (h ? t.accentHot : t.accent) : (h ? t.controlHot : t.control);
        D2D1_COLOR_F fg = b.primary ? t.onAccent : t.text;
        if (!b.enabled) {
            bg.a = 0.5f;
            fg.a = 0.45f;
        }
        fillRound(r, 6, bg);
        const float th = textHeight(b.text, fControl_.get(), bw);
        drawText(b.text, fControl_.get(), x, y_ + (kControlH - th) / 2, bw, fg);
        if (b.enabled) addHit(r, b.fn);
        x += bw + 8;
    }
    y_ += kControlH + kRowGap;
}

// Windows ekran ayarlarındaki biçim: "1600 × 940", cihazınki "(Önerilen)"; dikeyde boyutlar yer değiştirir
std::wstring Window::resolutionText(const Resolution& r) const {
    const bool portrait = model_.currentRotation % 180 != 0;
    std::wstring s = i18n::F(i18n::ResItem, portrait ? r.height : r.width, portrait ? r.width : r.height);
    if (r.width == model_.deviceWidth && r.height == model_.deviceHeight) s += W(i18n::Recommended);
    return s;
}

// --- Sayfalar ---

void Window::statusPage() {
    const Theme& t = theme_;
    const Model& m = model_;
    pageTitle(W(i18n::NavStatus));

    card([&] {
        dot(x_ + 6, y_ + 11, 6, m.connected ? t.ok : t.idle);
        y_ += drawText(m.connected ? std::wstring(W(i18n::Connected)) + L" · " + m.device : W(i18n::Waiting),
                       fSection_.get(), x_ + 20, y_, w_ - 20, t.text) + 12;
        if (!m.connected) {
            y_ += drawText(W(i18n::WaitingHint), fBody_.get(), x_, y_, w_, t.sub) + kRowGap;
            return;
        }
        std::wstring link = m.link;
        if (link.rfind(L"Wi-Fi", 0) == 0) link += std::wstring(L", ") + W(i18n::Encrypted);
        keyValue(W(i18n::RowLink), link);
        const DisplayMode dm = m.activeMode;
        keyValue(W(i18n::RowMode), W(dm == DisplayMode::Mirror ? i18n::ModeMirror
                                     : dm == DisplayMode::Only ? i18n::ModeOnly
                                                               : i18n::ModeExtend));
        if (dm != DisplayMode::Mirror && m.current.width > 0) {
            const int r = m.currentRotation;
            const wchar_t* orient = W(r == 90    ? i18n::OrientPortrait
                                      : r == 180 ? i18n::OrientLandscapeFlipped
                                      : r == 270 ? i18n::OrientPortraitFlipped
                                                 : i18n::OrientLandscape);
            keyValue(W(i18n::RowScreen), resolutionText(m.current) + L" · " + orient);
        }
        const std::wstring pending = W(i18n::StatsPending);
        keyValue(W(i18n::RowLatency), m.latency.empty() ? pending : m.latency);
        keyValue(W(i18n::RowData), m.data.empty() ? pending : m.data);
        keyValue(W(i18n::RowFps), m.fps.empty() ? pending : m.fps);
        y_ += kRowGap - 8;
    });

    if (m.notice != Notice(0)) {
        card(
            [&] {
                const bool missing = m.notice == Notice::AppMissing, tabletOld = m.notice == Notice::TabletOutdated;
                y_ += drawText(W(missing     ? i18n::NoticeAppMissingTitle
                                 : tabletOld ? i18n::NoticeTabletOldTitle
                                             : i18n::NoticePcOldTitle),
                               fSection_.get(), x_, y_, w_, t.warn) + 4;
                const std::wstring body = missing     ? W(i18n::NoticeAppMissing)
                                          : tabletOld ? W(i18n::NoticeTabletOld)
                                                      : i18n::F(i18n::NoticePcOld, m.noticeArg.c_str());
                y_ += drawText(body, fBody_.get(), x_, y_, w_, t.text) + kRowGap;
            },
            true);
    }

    if (!m.connected) {
        card([&] {
            y_ += drawText(W(i18n::StepsTitle), fSection_.get(), x_, y_, w_, t.text) + 8;
            for (i18n::Str s : {i18n::StepApp, i18n::StepUsb, i18n::StepWifi})
                y_ += drawText(W(s), fBody_.get(), x_, y_, w_, t.text) + 6;
            y_ += kRowGap - 6;
        });
        buttons({{W(i18n::ConnectNow), true, true, [this] { host_->act(Act::ConnectNow, 0); }}});
    } else if (m.activeMode != DisplayMode::Mirror) {
        buttons({{W(i18n::OpenDisplaySettings), false, true, [this] { host_->act(Act::OpenDisplaySettings, 0); }}});
    }
}

void Window::connectionPage() {
    const Model& m = model_;
    pageTitle(W(i18n::SecConnection));
    card([&] {
        label(W(i18n::LinkType));
        segmented({W(i18n::LinkAuto), W(i18n::LinkUsb), W(i18n::LinkWifi)}, int(m.opt.mode), {},
                  [this](int i) { host_->act(Act::Link, i); });
        hint(W(i18n::LinkHint), theme_.sub);
        divider();
        toggleRow(W(i18n::AutoConnect), W(i18n::AutoConnectHint), m.opt.autoConnect, true,
                  [this, v = m.opt.autoConnect] { host_->act(Act::AutoConnect, v ? 0 : 1); });
        buttons({{W(i18n::ConnectNow), true, !m.connected, [this] { host_->act(Act::ConnectNow, 0); }}});
    });
}

void Window::displayPage() {
    const Theme& t = theme_;
    const Model& m = model_;
    pageTitle(W(i18n::SecDisplay));
    const int mode = !m.opt.extend || !m.driver ? 2 : m.opt.onlyTablet ? 1 : 0;
    card([&] {
        label(W(i18n::DisplayMode));
        segmented({W(i18n::ModeExtend), W(i18n::ModeOnly), W(i18n::ModeMirror)}, mode, {m.driver, m.driver, true},
                  [this](int i) { host_->act(Act::Display, i); });
        hint(m.driver ? W(mode == 0 ? i18n::HintExtend : mode == 1 ? i18n::HintOnly : i18n::HintMirror)
                      : W(i18n::DriverMissing),
             m.driver ? t.sub : t.warn);
    });

    if (mode == 2) {
        card([&] {
            label(W(i18n::MirroredScreen));
            dropdown(m.outputs, m.opt.output, m.outputs.size() > 1, [this](int i) { host_->act(Act::Output, i); });
            y_ += kRowGap - 8;
        });
        return;
    }
    card([&] {
        label(W(i18n::Orientation));
        const int rot = m.opt.rotation < 0 ? 0 : m.opt.rotation / 90 + 1;
        dropdown({W(i18n::OrientAuto), W(i18n::OrientLandscape), W(i18n::OrientPortrait),
                  W(i18n::OrientLandscapeFlipped), W(i18n::OrientPortraitFlipped)},
                 rot, true, [this](int i) { host_->act(Act::Rotation, i == 0 ? -1 : (i - 1) * 90); });
        y_ += 6;
        label(W(i18n::Resolution));
        // Windows ekran ayarlarındaki listenin aynısı; cihaz bağlı değilken seçilemez
        std::vector<std::wstring> items;
        int sel = -1;
        for (size_t i = 0; i < m.modes.size(); i++) {
            items.push_back(resolutionText(m.modes[i]));
            if (m.modes[i].width == m.current.width && m.modes[i].height == m.current.height) sel = int(i);
        }
        if (items.empty()) {
            items.push_back(W(i18n::ResUnavailable));
            sel = 0;
        }
        dropdown(items, sel, !m.modes.empty(), [this](int i) { host_->act(Act::Resolution, i); });
        y_ += 6;
        hint(W(i18n::DisplaySyncHint), t.sub);
        buttons({{W(i18n::OpenDisplaySettings), false, true, [this] { host_->act(Act::OpenDisplaySettings, 0); }}});
    });
}

void Window::inputPage() {
    const Model& m = model_;
    pageTitle(W(i18n::SecInput));
    card([&] {
        label(W(i18n::Touch));
        segmented({W(i18n::TouchScreen), W(i18n::TouchMouse)}, m.opt.touchAsMouse ? 1 : 0, {},
                  [this](int i) { host_->act(Act::Touch, i); });
        hint(W(i18n::TouchHint), theme_.sub);
        divider();
        label(W(i18n::Performance));
        segmented({W(i18n::PerfBalanced), W(i18n::PerfLowLatency)}, m.opt.lowLatency ? 1 : 0, {},
                  [this](int i) { host_->act(Act::LowLatency, i); });
        hint(W(i18n::PerfHint), theme_.sub);
    });
}

void Window::generalPage() {
    const Model& m = model_;
    pageTitle(W(i18n::SecGeneral));
    card([&] {
        toggleRow(W(i18n::StartWithWindows), nullptr, m.autostart, true,
                  [this, v = m.autostart] { host_->act(Act::Autostart, v ? 0 : 1); });
        divider();
        toggleRow(W(i18n::Service), W(i18n::ServiceHint), m.serviceMode, true, [this] { host_->act(Act::Service, 0); });
        if (m.serviceMode && m.serviceStale) {
            hint(W(i18n::ServiceStale), theme_.warn);
            y_ -= kRowGap - 6;
            buttons({{W(i18n::ServiceUpdate), true, true, [this] { host_->act(Act::Service, 1); }}});
        }
        divider();
        label(W(i18n::Language));
        segmented({W(i18n::LangSystem), L"Türkçe", L"English"}, m.language, {},
                  [this](int i) { host_->act(Act::Language, i); });
        y_ += kRowGap - 8;
    });
    buttons({{W(i18n::OpenLog), false, true, [this] { host_->act(Act::OpenLog, 0); }},
             {W(i18n::Forget), false, true, [this] { host_->act(Act::Forget, 0); }}});
}

void Window::paint() {
    if (!ensureTarget()) return;
    const Theme& t = theme_;
    const D2D1_SIZE_F size = rt_->GetSize();
    viewH_ = size.height;

    rt_->BeginDraw();
    rt_->Clear(t.bg);
    hits_.clear();
    draw_ = true;

    // Sayfa (kayabilir), sonra üstüne sabit sol menü
    x_ = kSide + 8;
    w_ = std::max(200.f, size.width - x_ - kMargin);
    y_ = 22;
    switch (page_) {
        case kStatus: statusPage(); break;
        case kConnection: connectionPage(); break;
        case kDisplay: displayPage(); break;
        case kInput: inputPage(); break;
        case kGeneral: generalPage(); break;
    }
    contentH_ = y_ + 8;

    brush_->SetColor(t.bg);
    rt_->FillRectangle(D2D1::RectF(0, 0, kSide, size.height), brush_.get());
    sidebar(size.height);

    // Kaydırma çubuğu (yalnızca pencere sayfadan küçükse)
    if (contentH_ > viewH_) {
        const float trackH = viewH_ - 8, thumbH = std::max(32.f, trackH * viewH_ / contentH_);
        const float thumbY = 4 + (trackH - thumbH) * scroll_ / (contentH_ - viewH_);
        D2D1_COLOR_F c = t.sub;
        c.a = 0.5f;
        brush_->SetColor(c);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(size.width - 7, thumbY, size.width - 3, thumbY + thumbH), 2, 2),
                                  brush_.get());
    }

    if (rt_->EndDraw() == HRESULT(D2DERR_RECREATE_TARGET)) release();
}

LRESULT Window::proc(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            paint();
            // Kaydırma sınırı (içerik boyu değişmiş olabilir)
            const float maxScroll = std::max(0.f, contentH_ - viewH_);
            if (scroll_ > maxScroll) {
                scroll_ = maxScroll;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(l);
            mm->ptMinTrackSize.x = LONG(kMinWidth * dpi_ / 96.f);
            mm->ptMinTrackSize.y = LONG(kMinHeight * dpi_ / 96.f);
            return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = float(HIWORD(w));
            release();
            const RECT* r = reinterpret_cast<RECT*>(l);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (l && wcscmp(reinterpret_cast<const wchar_t*>(l), L"ImmersiveColorSet") == 0) {
                theme_ = makeTheme();
                applyTitleBarTheme();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            host_->act(Act::Reload, 0);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(w) != WA_INACTIVE) host_->act(Act::Reload, 0);  // ör. yön Windows ayarlarından değişti
            return 0;
        case WM_MOUSEWHEEL: {
            const float maxScroll = std::max(0.f, contentH_ - viewH_);
            scroll_ = std::clamp(scroll_ - float(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA * 60.f, 0.f, maxScroll);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const float s = 96.f / dpi_;
            if (!mouseIn_) {
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
            }
            mouseIn_ = true;
            mouse_ = D2D1::Point2F(float(GET_X_LPARAM(l)) * s, float(GET_Y_LPARAM(l)) * s);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE:
            mouseIn_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) {
                bool overHit = false;
                for (const Hit& h : hits_)
                    overHit |= mouseIn_ && inside(h.r, D2D1::Point2F(mouse_.x, mouse_.y + (h.fixed ? 0 : scroll_)));
                SetCursor(LoadCursorW(nullptr, overHit ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
            pressed_ = -1;
            for (size_t i = 0; i < hits_.size(); i++)
                if (inside(hits_[i].r, D2D1::Point2F(mouse_.x, mouse_.y + (hits_[i].fixed ? 0 : scroll_))))
                    pressed_ = int(i);
            SetCapture(hwnd_);
            return 0;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            const int p = pressed_;
            pressed_ = -1;
            if (p >= 0 && p < int(hits_.size()) &&
                inside(hits_[size_t(p)].r, D2D1::Point2F(mouse_.x, mouse_.y + (hits_[size_t(p)].fixed ? 0 : scroll_)))) {
                auto fn = hits_[size_t(p)].fn;  // işlem pencereyi yeniden çizdirebilir (hits_ değişir)
                fn();
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) DestroyWindow(hwnd_);
            else if (w == VK_UP || w == VK_PRIOR) setPage(page_ - 1);
            else if (w == VK_DOWN || w == VK_NEXT) setPage(page_ + 1);
            else if (w == VK_TAB && GetKeyState(VK_CONTROL) < 0) setPage(page_ + (GetKeyState(VK_SHIFT) < 0 ? -1 : 1));
            else if (w >= '1' && w <= '5') setPage(int(w - '1'));
            return 0;
        case WM_DESTROY: {
            g_window = nullptr;
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);  // sonraki iletiler (WM_NCDESTROY) silinmiş nesneye gitmesin
            Host* host = host_;
            delete this;
            host->act(Act::Closed, 0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}

}  // namespace

void show(Host* host, HICON icon) {
    if (g_window) {
        HWND h = g_window->hwnd();
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        return;
    }
    auto* w = new Window(host, icon);
    g_window = w;
    if (!w->create()) {
        g_window = nullptr;
        delete w;
    }
}

void refresh() {
    if (g_window) g_window->refresh();
}

void close() {
    if (g_window) DestroyWindow(g_window->hwnd());
}

HWND window() { return g_window ? g_window->hwnd() : nullptr; }

}  // namespace ekran::ui
