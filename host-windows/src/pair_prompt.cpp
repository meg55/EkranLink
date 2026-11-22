#include "pair_prompt.h"

#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>

#include "com.h"
#include "config.h"
#include "i18n.h"
#include "log.h"
#include "theme.h"

namespace ekran {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

namespace {

using i18n::W;
using theme::Theme;

// Eşleştirme sorusu: ayar penceresiyle aynı görünüm (Direct2D, Windows teması). Varsayılan düğme Reddet: başka bir
// şey yazarken basılan Enter / boşluk eşleştirmeyi onaylamasın. Esc ve pencereyi kapatmak da reddeder.
class PairWindow {
public:
    PairWindow(std::wstring device, std::wstring code) : device_(std::move(device)), code_(std::move(code)) {}

    // Engelleyici: pencere kapanana kadar ileti döngüsü. hwndOut: dışarıdan kapatmak için (WM_CLOSE).
    bool run(std::atomic<HWND>& hwndOut) {
        if (!create()) return false;
        hwndOut = hwnd_;
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        hwndOut = nullptr;
        return accepted_;
    }

private:
    static constexpr float kW = 460, kPad = 24, kBtnH = 34, kBtnGap = 8;

    std::wstring device_, code_;
    HWND hwnd_ = nullptr;
    float dpi_ = 96;
    Theme t_;
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dw_;
    ComPtr<ID2D1HwndRenderTarget> rt_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> fTitle_, fName_, fLabel_, fCode_, fBody_, fSmall_, fBtn_, fIcon_, fIconSmall_;
    D2D1_RECT_F btn_[2]{};  // 0: Reddet, 1: Eşleştir
    int focus_ = 0, hot_ = -1, pressed_ = -1;
    bool accepted_ = false;

    bool create() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                     reinterpret_cast<void**>(d2d_.put()))) ||
            FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dw_.put()))))
            return false;
        auto fmt = [&](ComPtr<IDWriteTextFormat>& f, const wchar_t* family, DWRITE_FONT_WEIGHT w, float size,
                       bool center = false) {
            dw_->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"",
                                  f.put());
            f->SetWordWrapping(center ? DWRITE_WORD_WRAPPING_NO_WRAP : DWRITE_WORD_WRAPPING_WRAP);
            if (center) {
                f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        };
        fmt(fTitle_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 18);
        fmt(fName_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 14);
        fmt(fLabel_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12, true);
        fmt(fCode_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 40, true);
        fmt(fBody_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 13);
        fmt(fSmall_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12);
        fmt(fBtn_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 13, true);
        {
            ComPtr<IDWriteFontCollection> fonts;
            UINT32 idx = 0;
            BOOL found = FALSE;
            dw_->GetSystemFontCollection(fonts.put(), FALSE);
            if (fonts) fonts->FindFamilyName(L"Segoe Fluent Icons", &idx, &found);
            const wchar_t* icons = found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
            fmt(fIcon_, icons, DWRITE_FONT_WEIGHT_NORMAL, 20, true);
            fmt(fIconSmall_, icons, DWRITE_FONT_WEIGHT_NORMAL, 14, true);
        }
        // Hizmet ajanı SYSTEM'dir, kullanıcının temasını tepsi yazar ("UiDark"); yoksa bu sürecin kullanıcısınınki
        const DWORD dark = config::get(L"UiDark", 2);
        t_ = theme::makeTheme(dark == 2 ? theme::systemDark() : dark == 1);

        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{sizeof wc};
            wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                if (m == WM_NCCREATE) {
                    auto* self = static_cast<PairWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
                    self->hwnd_ = h;
                    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                }
                auto* self = reinterpret_cast<PairWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
                return self ? self->proc(m, w, l) : DefWindowProcW(h, m, w, l);
            };
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = L"EkranLinkEslestirme";
            RegisterClassExW(&wc);
            registered = true;
        }
        constexpr DWORD style = WS_CAPTION | WS_SYSMENU, exStyle = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
        hwnd_ = CreateWindowExW(exStyle, L"EkranLinkEslestirme", W(i18n::PairTitle), style, CW_USEDEFAULT,
                                CW_USEDEFAULT, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd_) return false;
        const int big = GetSystemMetricsForDpi(SM_CXICON, GetDpiForWindow(hwnd_));
        const int small = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(hwnd_));
        for (auto [which, size] : {std::pair{ICON_BIG, big}, std::pair{ICON_SMALL, small}})
            SendMessageW(hwnd_, WM_SETICON, which,
                         reinterpret_cast<LPARAM>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                                                             IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED)));
        const BOOL darkBar = t_.dark;
        DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkBar, sizeof darkBar);

        // İmlecin bulunduğu ekranın ortasında; boyut içeriğe göre
        POINT pt;
        GetCursorPos(&pt);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowPos(hwnd_, nullptr, mi.rcWork.left, mi.rcWork.top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
        dpi_ = float(GetDpiForWindow(hwnd_));
        RECT r = sizeFor(dpi_);
        const int w = r.right - r.left, h = r.bottom - r.top;
        SetWindowPos(hwnd_, nullptr, (mi.rcWork.left + mi.rcWork.right - w) / 2, (mi.rcWork.top + mi.rcWork.bottom - h) / 2,
                     w, h, SWP_NOZORDER | SWP_NOACTIVATE);

        // İlk ShowWindow başlatanın isteğini (ör. gizli) uygulayabilir; soru her durumda görünsün
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        if (GetForegroundWindow() != hwnd_) {
            FLASHWINFO fi{sizeof fi, hwnd_, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
            FlashWindowEx(&fi);
        }
        SetFocus(hwnd_);
        return true;
    }

    // Pencere boyutu (içerik yüksekliği metne göre)
    RECT sizeFor(float dpi) {
        const float s = dpi / 96.f;
        RECT r{0, 0, LONG(kW * s + 0.5f), LONG(layout(false) * s + 0.5f)};
        AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, UINT(dpi));
        return r;
    }

    ComPtr<IDWriteTextLayout> text(const std::wstring& s, IDWriteTextFormat* f, float w, float h = 10000.f) {
        ComPtr<IDWriteTextLayout> l;
        dw_->CreateTextLayout(s.c_str(), UINT32(s.size()), f, std::max(w, 1.f), h, l.put());
        return l;
    }

    float height(IDWriteTextLayout* l) {
        DWRITE_TEXT_METRICS m{};
        if (l) l->GetMetrics(&m);
        return m.height;
    }

    float width(IDWriteTextLayout* l) {
        DWRITE_TEXT_METRICS m{};
        if (l) l->GetMetrics(&m);
        return m.widthIncludingTrailingWhitespace;
    }

    void fill(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c) {
        brush_->SetColor(c);
        if (radius > 0) rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.get());
        else rt_->FillRectangle(r, brush_.get());
    }

    void stroke(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c, float w = 1.f) {
        brush_->SetColor(c);
        const float h = w / 2;
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + h, r.top + h, r.right - h, r.bottom - h),
                                                    radius, radius),
                                  brush_.get(), w);
    }

    void put(IDWriteTextLayout* l, float x, float y, D2D1_COLOR_F c) {
        brush_->SetColor(c);
        rt_->DrawTextLayout(D2D1::Point2F(x, y), l, brush_.get());
    }

    // Yerleşim; draw = false iken yalnızca yüksekliği hesaplar. Döndürdüğü: istemci alanı yüksekliği (DIP).
    float layout(bool draw) {
        const float inner = kW - 2 * kPad;
        float y = kPad;

        // Başlık: telefon simgesi, "Yeni bir cihaz bağlanmak istiyor", cihazın adı
        const float iconD = 44, tx = kPad + iconD + 14;
        auto title = text(W(i18n::PairHeading), fTitle_.get(), kW - kPad - tx);
        auto name = text(device_, fName_.get(), kW - kPad - tx);
        const float th = height(title.get()), nh = height(name.get());
        const float headH = std::max(iconD, th + 2 + nh);
        if (draw) {
            const float cy = y + headH / 2;
            brush_->SetColor(t_.accent);
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kPad + iconD / 2, cy), iconD / 2, iconD / 2), brush_.get());
            auto glyph = text(L"\xE8EA", fIcon_.get(), iconD, iconD);  // Segoe Fluent Icons: CellPhone
            put(glyph.get(), kPad, cy - iconD / 2, t_.onAccent);
            const float ty = y + (headH - (th + 2 + nh)) / 2;
            put(title.get(), tx, ty, t_.text);
            put(name.get(), tx, ty + th + 2, t_.sub);
        }
        y += headH + 20;

        // Kod kartı
        auto label = text(W(i18n::PairCodeLabel), fLabel_.get(), inner, 18);
        // Rakam grupları arasına ince boşluk: "282 066" iki grup olarak okunur
        std::wstring spaced;
        for (wchar_t c : code_) {
            if (!spaced.empty()) spaced += L'\x2009';
            spaced += c == L' ' ? L'\x2002' : c;
        }
        auto code = text(spaced, fCode_.get(), inner, 56);
        const float cardH = 14 + 18 + 4 + 56 + 14;
        if (draw) {
            const D2D1_RECT_F card = D2D1::RectF(kPad, y, kW - kPad, y + cardH);
            fill(card, 8, t_.bg);
            stroke(card, 8, t_.border);
            put(label.get(), kPad, y + 14, t_.sub);
            put(code.get(), kPad, y + 14 + 18 + 4, t_.text);
        }
        y += cardH + 18;

        // Ne yapılacak, uyarı, bir kez sorulur
        auto check = text(W(i18n::PairCheck), fBody_.get(), inner);
        if (draw) put(check.get(), kPad, y, t_.text);
        y += height(check.get()) + 12;

        const float wx = kPad + 22;
        auto warn = text(W(i18n::PairWarn), fSmall_.get(), kW - kPad - wx);
        if (draw) {
            auto shield = text(L"\xEA18", fIconSmall_.get(), 16, 18);  // Shield
            put(shield.get(), kPad, y, t_.warn);
            put(warn.get(), wx, y, t_.warn);
        }
        y += height(warn.get()) + 8;

        auto once = text(W(i18n::PairOnce), fSmall_.get(), inner);
        if (draw) put(once.get(), kPad, y, t_.sub);
        y += height(once.get()) + 24;

        // Alt bant: düğmeler sağda
        const float footTop = y;
        const float footH = kBtnH + 2 * 18;
        const wchar_t* labels[2] = {W(i18n::PairDecline), W(i18n::PairAccept)};
        float bw[2];
        for (int i = 0; i < 2; i++) bw[i] = std::max(110.f, width(text(labels[i], fBtn_.get(), 1000, kBtnH).get()) + 40);
        float bx = kW - kPad - bw[1] - kBtnGap - bw[0];
        const float by = footTop + 18;
        for (int i = 0; i < 2; i++) {
            btn_[i] = D2D1::RectF(bx, by, bx + bw[i], by + kBtnH);
            bx += bw[i] + kBtnGap;
        }
        if (draw) {
            fill(D2D1::RectF(0, footTop, kW, footTop + footH), 0, t_.bg);
            fill(D2D1::RectF(0, footTop, kW, footTop + 1), 0, t_.border);
            for (int i = 0; i < 2; i++) {
                const bool primary = i == 1, hot = hot_ == i;
                D2D1_COLOR_F bg = primary ? (hot ? t_.accentHot : t_.accent) : (hot ? t_.controlHot : t_.control);
                if (pressed_ == i) bg.a = 0.85f;
                fill(btn_[i], 6, bg);
                if (!primary) stroke(btn_[i], 6, t_.border);
                if (focus_ == i) {
                    const D2D1_RECT_F f = D2D1::RectF(btn_[i].left - 3, btn_[i].top - 3, btn_[i].right + 3,
                                                      btn_[i].bottom + 3);
                    stroke(f, 8, t_.text, 2);
                }
                auto l = text(labels[i], fBtn_.get(), btn_[i].right - btn_[i].left, kBtnH);
                put(l.get(), btn_[i].left, btn_[i].top, primary ? t_.onAccent : t_.text);
            }
        }
        return footTop + footH;
    }

    void paint() {
        if (!rt_) {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
            props.dpiX = props.dpiY = dpi_;
            if (FAILED(d2d_->CreateHwndRenderTarget(
                    props, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(UINT32(rc.right), UINT32(rc.bottom))),
                    rt_.put())))
                return;
            rt_->CreateSolidColorBrush(t_.text, brush_.put());
        }
        rt_->BeginDraw();
        rt_->Clear(t_.card);
        layout(true);
        if (rt_->EndDraw() == HRESULT(D2DERR_RECREATE_TARGET)) {
            brush_.reset();
            rt_.reset();
        }
    }

    int hitTest(LPARAM l) const {
        const float s = 96.f / dpi_;
        const float x = float(GET_X_LPARAM(l)) * s, y = float(GET_Y_LPARAM(l)) * s;
        for (int i = 0; i < 2; i++)
            if (x >= btn_[i].left && x < btn_[i].right && y >= btn_[i].top && y < btn_[i].bottom) return i;
        return -1;
    }

    void finish(bool accept) {
        accepted_ = accept;
        DestroyWindow(hwnd_);
    }

    void redraw() { InvalidateRect(hwnd_, nullptr, FALSE); }

    LRESULT proc(UINT m, WPARAM w, LPARAM l) {
        switch (m) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd_, &ps);
                paint();
                EndPaint(hwnd_, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
                if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
                redraw();
                return 0;
            case WM_DPICHANGED: {
                dpi_ = float(HIWORD(w));
                brush_.reset();
                rt_.reset();
                const RECT* r = reinterpret_cast<const RECT*>(l);
                const RECT want = sizeFor(dpi_);
                SetWindowPos(hwnd_, nullptr, r->left, r->top, want.right - want.left, want.bottom - want.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case WM_MOUSEMOVE: {
                const int h = hitTest(l);
                if (h != hot_) {
                    hot_ = h;
                    redraw();
                    TRACKMOUSEEVENT te{sizeof te, TME_LEAVE, hwnd_, 0};
                    TrackMouseEvent(&te);
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                hot_ = -1;
                redraw();
                return 0;
            case WM_LBUTTONDOWN:
                pressed_ = hitTest(l);
                if (pressed_ >= 0) {
                    focus_ = pressed_;
                    SetCapture(hwnd_);
                }
                redraw();
                return 0;
            case WM_LBUTTONUP: {
                const int p = pressed_;
                pressed_ = -1;
                ReleaseCapture();
                if (p >= 0 && hitTest(l) == p) finish(p == 1);
                else redraw();
                return 0;
            }
            case WM_GETDLGCODE:
                return DLGC_WANTALLKEYS;
            case WM_KEYDOWN:
                switch (w) {
                    case VK_TAB: case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
                        focus_ = 1 - focus_;
                        redraw();
                        return 0;
                    case VK_RETURN: case VK_SPACE:
                        finish(focus_ == 1);
                        return 0;
                    case VK_ESCAPE:
                        finish(false);
                        return 0;
                }
                break;
            case WM_CLOSE:
                finish(false);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd_, m, w, l);
    }
};

struct PairAsk {
    std::wstring device, code;
    std::atomic<HWND> hwnd{nullptr};
    bool accepted = false;
};

DWORD WINAPI pairAskThread(void* p) {
    auto* a = static_cast<PairAsk*>(p);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        PairWindow w(a->device, a->code);
        a->accepted = w.run(a->hwnd);
    }
    CoUninitialize();
    return 0;
}

std::atomic<bool> g_asking{false};

}  // namespace

bool askPairing(const std::wstring& device, const std::wstring& code, HANDLE cancel) {
    // Aynı anda tek soru: üst üste açılan sorular karıştırılıp yanlışlıkla onaylanmasın
    if (g_asking.exchange(true)) {
        logf("Başka bir eşleştirme sorusu açık; bu istek reddedildi");
        return false;
    }
    struct Done {
        ~Done() { g_asking = false; }
    } done;
    PairAsk a;
    a.device = device;
    a.code = code;
    HANDLE th = CreateThread(nullptr, 0, pairAskThread, &a, 0, nullptr);
    if (!th) return false;
    HANDLE hs[2] = {th, cancel};
    if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        logf("Cihaz eşleştirmeyi reddetti; soru kapatılıyor");
        // Pencere henüz açılmamış olabilir: açılınca kapatılır
        for (int i = 0; i < 50 && WaitForSingleObject(th, 100) == WAIT_TIMEOUT; i++)
            if (HWND h = a.hwnd.load()) PostMessageW(h, WM_CLOSE, 0, 0);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
        return false;
    }
    CloseHandle(th);
    return a.accepted;
}

}  // namespace ekran
