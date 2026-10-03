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
    D2D1_COLOR_F bg, card, border, text, sub, control, controlHot, accent, accentHot, onAccent, ok, idle, warn;
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
        t.warn = rgb(0xFF8A65);
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
        t.warn = rgb(0xC4472B);
    }
    return t;
}

// --- Ölçüler (DIP) ---

constexpr float kWidth = 540, kHeight = 780, kMinWidth = 440;
constexpr float kMargin = 20, kCardPad = 16, kRowGap = 14, kControlH = 32, kRadius = 8;

struct Hit {
    D2D1_RECT_F r;
    std::function<void()> fn;
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

    // Yerleşim + çizim: her boyamada baştan (anlık mod). draw_ false iken yalnızca ölçülür.
    float textHeight(const std::wstring& s, IDWriteTextFormat* f, float w);
    float drawText(const std::wstring& s, IDWriteTextFormat* f, float x, float y, float w, D2D1_COLOR_F c);
    float textWidth(const std::wstring& s, IDWriteTextFormat* f);
    void fillRound(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c);
    void strokeRound(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c, float width = 1.f);
    bool hot(const D2D1_RECT_F& r) const { return mouseIn_ && inside(r, mouse_); }
    void addHit(const D2D1_RECT_F& r, std::function<void()> fn) {
        if (draw_) hits_.push_back({r, std::move(fn)});
    }

    void header();
    void card(const wchar_t* title, const std::function<void()>& body);
    void divider();
    void label(const wchar_t* text, const wchar_t* hint);
    void hint(const std::wstring& text, D2D1_COLOR_F color);
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

    Host* host_;
    HICON icon_;
    HWND hwnd_ = nullptr;
    Model model_;
    Theme theme_;
    float dpi_ = 96;

    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID2D1HwndRenderTarget> rt_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> fTitle_, fSection_, fLabel_, fBody_, fHint_, fControl_;

    bool draw_ = true;
    float x_ = 0, y_ = 0, w_ = 0;  // geçerli içerik sütunu (içerik koordinatı)
    float scroll_ = 0, contentH_ = 0, viewH_ = 0;
    std::vector<Hit> hits_;
    D2D1_POINT_2F mouse_{};
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
    auto fmt = [&](ComPtr<IDWriteTextFormat>& f, DWRITE_FONT_WEIGHT weight, float size) {
        dwrite_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                  size, L"", f.put());
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    };
    fmt(fTitle_, DWRITE_FONT_WEIGHT_SEMI_BOLD, 26);
    fmt(fSection_, DWRITE_FONT_WEIGHT_SEMI_BOLD, 14);
    fmt(fLabel_, DWRITE_FONT_WEIGHT_NORMAL, 14);
    fmt(fBody_, DWRITE_FONT_WEIGHT_NORMAL, 13);
    fmt(fHint_, DWRITE_FONT_WEIGHT_NORMAL, 12);
    fmt(fControl_, DWRITE_FONT_WEIGHT_NORMAL, 13);
    fControl_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fControl_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
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
    const int w = int(kWidth * s), h = std::min(int(kHeight * s), int(wa.bottom - wa.top) - int(40 * s));
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
        rt_->DrawTextLayout(D2D1::Point2F(x, y - scroll_), l.get(), brush_.get());
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
    rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left, r.top - scroll_, r.right, r.bottom - scroll_),
                                                radius, radius),
                              brush_.get());
}

void Window::strokeRound(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c, float width) {
    if (!draw_) return;
    brush_->SetColor(c);
    const float h = width / 2;
    rt_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(r.left + h, r.top + h - scroll_, r.right - h, r.bottom - h - scroll_), radius,
                          radius),
        brush_.get(), width);
}

// --- Bileşenler ---

void Window::header() {
    const Theme& t = theme_;
    y_ += drawText(L"EKRAN", fTitle_.get(), x_, y_, w_, t.text) + 6;

    const bool on = model_.connected;
    // Durum noktası + başlık
    const float dot = 10;
    if (draw_) {
        brush_->SetColor(on ? t.ok : t.idle);
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x_ + dot / 2, y_ + 10 - scroll_), dot / 2, dot / 2), brush_.get());
    }
    std::wstring line = on ? std::wstring(W(i18n::Connected)) + L" · " + model_.device : W(i18n::Waiting);
    y_ += drawText(line, fLabel_.get(), x_ + dot + 8, y_, w_ - dot - 8, t.text) + 2;

    std::wstring detail;
    if (on) {
        const wchar_t* mode = model_.activeMode == DisplayMode::Mirror ? W(i18n::ModeMirrorShort)
                              : model_.activeMode == DisplayMode::Only ? W(i18n::ModeOnlyShort)
                                                                        : W(i18n::ModeExtendShort);
        detail = model_.link;
        if (model_.link.rfind(L"Wi-Fi", 0) == 0) detail += std::wstring(L", ") + W(i18n::Encrypted);
        detail += std::wstring(L" · ") + mode;
        if (!model_.stats.empty()) detail += L" · " + model_.stats;
    } else {
        detail = W(i18n::WaitingHint);
    }
    y_ += drawText(detail, fHint_.get(), x_ + dot + 8, y_, w_ - dot - 8, t.sub) + 18;
}

void Window::card(const wchar_t* title, const std::function<void()>& body) {
    y_ += drawText(title, fSection_.get(), x_ + 2, y_, w_, theme_.text) + 8;
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
    fillRound(r, kRadius, theme_.card);
    strokeRound(r, kRadius, theme_.border);
    y_ = top + kCardPad;
    body();

    x_ = x0;
    w_ = w0;
    y_ = bottom + 22;
}

void Window::divider() {
    if (draw_) {
        brush_->SetColor(theme_.border);
        // Piksel ortasına hizala: yoksa 1 piksellik çizgi iki satıra yayılıp soluklaşır
        const float px = dpi_ / 96.f;
        const float y = (std::floor((y_ - kRowGap / 2 - scroll_) * px) + 0.5f) / px;
        rt_->DrawLine(D2D1::Point2F(x_, y), D2D1::Point2F(x_ + w_, y), brush_.get(), 1.f);
    }
}

void Window::label(const wchar_t* text, const wchar_t* hintText) {
    y_ += drawText(text, fLabel_.get(), x_, y_, w_, theme_.text) + 6;
    (void)hintText;
}

void Window::hint(const std::wstring& text, D2D1_COLOR_F color) {
    y_ += drawText(text, fHint_.get(), x_, y_, w_, color) + kRowGap;
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
    if (!enabled) c.a = 0.4f;
    const std::wstring text = sel >= 0 && sel < int(items.size()) ? items[size_t(sel)] : L"";
    const float th = textHeight(text, fBody_.get(), w_ - 40);
    drawText(text, fBody_.get(), x_ + 12, y_ + (kControlH - th) / 2, w_ - 40, c);
    if (draw_) {  // aşağı ok
        brush_->SetColor(c);
        const float cx = x_ + w_ - 18, cy = y_ + kControlH / 2 - scroll_;
        rt_->DrawLine(D2D1::Point2F(cx - 5, cy - 2.5f), D2D1::Point2F(cx, cy + 2.5f), brush_.get(), 1.5f);
        rt_->DrawLine(D2D1::Point2F(cx, cy + 2.5f), D2D1::Point2F(cx + 5, cy - 2.5f), brush_.get(), 1.5f);
    }
    if (enabled) {
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
    // Anahtar
    const D2D1_RECT_F s = D2D1::RectF(x_ + w_ - sw, top + 1, x_ + w_, top + 1 + sh);
    const D2D1_RECT_F row = D2D1::RectF(x_, top - 4, x_ + w_, top + rowH + 4);
    D2D1_COLOR_F fill = value ? (enabled && hot(row) ? t.accentHot : t.accent) : (enabled && hot(row) ? t.controlHot : t.control);
    if (!enabled) fill.a = 0.5f;
    fillRound(s, sh / 2, fill);
    if (!value) strokeRound(s, sh / 2, t.sub, 1.f);
    if (draw_) {
        D2D1_COLOR_F knob = value ? t.onAccent : t.sub;
        if (!enabled) knob.a = 0.5f;
        brush_->SetColor(knob);
        const float r = value ? 7 : 6;
        const float cx = value ? s.right - 10 : s.left + 10;
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, (s.top + s.bottom) / 2 - scroll_), r, r), brush_.get());
    }
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

// --- Sayfa ---

void Window::paint() {
    if (!ensureTarget()) return;
    const Theme& t = theme_;
    const Model& m = model_;
    const D2D1_SIZE_F size = rt_->GetSize();
    viewH_ = size.height;

    rt_->BeginDraw();
    rt_->Clear(t.bg);
    hits_.clear();
    draw_ = true;
    x_ = kMargin;
    w_ = size.width - 2 * kMargin;
    y_ = kMargin;

    header();

    // Bağlantı
    card(W(i18n::SecConnection), [&] {
        label(W(i18n::LinkType), nullptr);
        segmented({W(i18n::LinkAuto), W(i18n::LinkUsb), W(i18n::LinkWifi)}, int(m.opt.mode), {},
                  [this](int i) { host_->act(Act::Link, i); });
        hint(W(i18n::LinkHint), t.sub);
        divider();
        toggleRow(W(i18n::AutoConnect), W(i18n::AutoConnectHint), m.opt.autoConnect, true,
                  [this, v = m.opt.autoConnect] { host_->act(Act::AutoConnect, v ? 0 : 1); });
        buttons({{W(i18n::ConnectNow), true, !m.connected, [this] { host_->act(Act::ConnectNow, 0); }}});
    });

    // Görüntü
    card(W(i18n::SecDisplay), [&] {
        const int mode = !m.opt.extend || !m.driver ? 2 : m.opt.onlyTablet ? 1 : 0;
        label(W(i18n::DisplayMode), nullptr);
        segmented({W(i18n::ModeExtend), W(i18n::ModeOnly), W(i18n::ModeMirror)}, mode, {m.driver, m.driver, true},
                  [this](int i) { host_->act(Act::Display, i); });
        hint(m.driver ? W(mode == 0 ? i18n::HintExtend : mode == 1 ? i18n::HintOnly : i18n::HintMirror)
                      : W(i18n::DriverMissing),
             m.driver ? t.sub : t.warn);
        divider();
        label(W(i18n::Orientation), nullptr);
        const int rot = m.opt.rotation < 0 ? 0 : m.opt.rotation / 90 + 1;
        dropdown({W(i18n::OrientAuto), W(i18n::OrientLandscape), W(i18n::OrientPortrait),
                  W(i18n::OrientLandscapeFlipped), W(i18n::OrientPortraitFlipped)},
                 rot, mode != 2, [this](int i) { host_->act(Act::Rotation, i == 0 ? -1 : (i - 1) * 90); });
        hint(mode != 2 ? W(i18n::OrientHint) : W(i18n::OrientMirrorHint), t.sub);
        if (mode == 2) {
            divider();
            label(W(i18n::MirroredScreen), nullptr);
            dropdown(m.outputs, m.opt.output, m.outputs.size() > 1, [this](int i) { host_->act(Act::Output, i); });
            y_ += kRowGap - 8;
        }
    });

    // Dokunma ve performans
    card(W(i18n::SecInput), [&] {
        label(W(i18n::Touch), nullptr);
        segmented({W(i18n::TouchScreen), W(i18n::TouchMouse)}, m.opt.touchAsMouse ? 1 : 0, {},
                  [this](int i) { host_->act(Act::Touch, i); });
        hint(W(i18n::TouchHint), t.sub);
        divider();
        label(W(i18n::Performance), nullptr);
        segmented({W(i18n::PerfBalanced), W(i18n::PerfLowLatency)}, m.opt.lowLatency ? 1 : 0, {},
                  [this](int i) { host_->act(Act::LowLatency, i); });
        hint(W(i18n::PerfHint), t.sub);
    });

    // Genel
    card(W(i18n::SecGeneral), [&] {
        toggleRow(W(i18n::StartWithWindows), nullptr, m.autostart, true,
                  [this, v = m.autostart] { host_->act(Act::Autostart, v ? 0 : 1); });
        divider();
        toggleRow(W(i18n::Service), W(i18n::ServiceHint), m.serviceMode, true,
                  [this] { host_->act(Act::Service, 0); });
        if (m.serviceMode && m.serviceStale) {
            hint(W(i18n::ServiceStale), t.warn);
            y_ -= kRowGap - 6;
            buttons({{W(i18n::ServiceUpdate), true, true, [this] { host_->act(Act::Service, 1); }}});
        }
        divider();
        label(W(i18n::Language), nullptr);
        segmented({W(i18n::LangSystem), L"Türkçe", L"English"}, m.language, {},
                  [this](int i) { host_->act(Act::Language, i); });
        y_ += kRowGap - 8;
        divider();
        buttons({{W(i18n::OpenLog), false, true, [this] { host_->act(Act::OpenLog, 0); }},
                 {W(i18n::Forget), false, true, [this] { host_->act(Act::Forget, 0); }}});
    });

    y_ += drawText(i18n::F(i18n::Version, config::kVersion) + L" · GPL-3.0", fHint_.get(), x_ + 2, y_ - 8, w_, t.sub);
    contentH_ = y_ + kMargin;

    // Kaydırma çubuğu
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
            mm->ptMinTrackSize.y = LONG(320 * dpi_ / 96.f);
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
            const D2D1_POINT_2F p = D2D1::Point2F(float(GET_X_LPARAM(l)) * s, float(GET_Y_LPARAM(l)) * s + scroll_);
            if (!mouseIn_) {
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
            }
            mouseIn_ = true;
            mouse_ = p;
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
                for (const Hit& h : hits_) overHit |= mouseIn_ && inside(h.r, mouse_);
                SetCursor(LoadCursorW(nullptr, overHit ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
            pressed_ = -1;
            for (size_t i = 0; i < hits_.size(); i++)
                if (inside(hits_[i].r, mouse_)) pressed_ = int(i);
            SetCapture(hwnd_);
            return 0;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            const int p = pressed_;
            pressed_ = -1;
            if (p >= 0 && p < int(hits_.size()) && inside(hits_[size_t(p)].r, mouse_)) {
                auto fn = hits_[size_t(p)].fn;  // işlem pencereyi yeniden çizdirebilir (hits_ değişir)
                fn();
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            g_window = nullptr;
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);  // sonraki iletiler (WM_NCDESTROY) silinmiş nesneye gitmesin
            delete this;
            return 0;
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
