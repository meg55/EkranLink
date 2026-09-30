// EKRAN — bildirim alanı (tepsi) uygulaması. Pencere yok; saatin yanındaki simgeden yönetilir.
// Boştayken hiçbir şey yapmaz: tablet yoklaması 3 saniyede bir, yayın yalnızca bağlıyken.

#include <winsock2.h>
#include <windows.h>
#include <dxgi.h>
#include <mfapi.h>
#include <shellapi.h>

#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "adb.h"
#include "apkserver.h"
#include "config.h"
#include "log.h"
#include "pair_prompt.h"
#include "pairing.h"
#include "service.h"
#include "streamer.h"
#include "virtual_display.h"

using namespace ekran;

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_STATE = WM_APP + 2;   // lParam: StateMsg*
constexpr UINT WM_STATS = WM_APP + 3;   // lParam: std::string*
constexpr UINT kTrayId = 1;

enum MenuId : UINT {
    kMenuAuto = 100,
    kMenuUsb,
    kMenuWifi,
    kMenuAutostart,
    kMenuLog,
    kMenuForget,
    kMenuInstall,
    kMenuExtend,
    kMenuOnly,
    kMenuMirror,
    kMenuTouch,
    kMenuMouse,
    kMenuBalanced,
    kMenuLowLatency,
    kMenuService,
    kMenuAutoConnect,
    kMenuConnectNow,
    kMenuExit,
    kMenuOutputBase = 200,
};

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

struct StateMsg {
    bool connected;
    std::string device, link;
};

HWND g_wnd = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_iconIdle = nullptr, g_iconOn = nullptr;
Options g_opt;
std::unique_ptr<Streamer> g_streamer;
std::thread g_worker;
bool g_connected = false;
std::wstring g_device, g_link, g_stats;
std::wstring g_logPath;
ApkServer g_apkServer;
// Hizmet kuruluysa yayını hizmet (ajan) yapar; tepsi yalnızca arayüzdür: ayarları HKLM'e yazar,
// durumu oradan okur. Değilse yayını tepsi kendisi yapar (ayarlar HKCU).
bool g_serviceMode = false;
HANDLE g_statusQuit = nullptr;
std::thread g_statusWatcher;

// --- Ayarlar (config.h: kullanıcı ya da makine deposu) ---

void regSet(const wchar_t* name, DWORD v) { config::set(name, v); }

std::wstring exePath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

bool autostartEnabled() {
    wchar_t v[MAX_PATH * 2];
    DWORD size = sizeof v;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"EKRAN", RRF_RT_REG_SZ, nullptr, v, &size) == 0;
}

void setAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != 0) return;
    if (on) {
        std::wstring cmd = L"\"" + exePath() + L"\"";
        RegSetValueExW(k, L"EKRAN", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"EKRAN");
    }
    RegCloseKey(k);
}

// --- Günlük dosyası ---

void openLog() {
    wchar_t dir[MAX_PATH];
    GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    std::wstring d = std::wstring(dir) + L"\\EKRAN";
    CreateDirectoryW(d.c_str(), nullptr);
    g_logPath = d + L"\\ekran.log";
    logToFile(g_logPath);
}

// --- Simge: kodla çizilmiş küçük monitör ---

HICON makeIcon(COLORREF screen) {
    const int S = 32;
    BITMAPV5HEADER bh{};
    bh.bV5Size = sizeof bh;
    bh.bV5Width = S;
    bh.bV5Height = -S;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000;
    bh.bV5GreenMask = 0x0000FF00;
    bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bh), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    auto* px = static_cast<uint32_t*>(bits);
    const uint32_t frame = 0xFFF0F0F0;
    const uint32_t fill = 0xFF000000 | (uint32_t(GetRValue(screen)) << 16) | (uint32_t(GetGValue(screen)) << 8) |
                          GetBValue(screen);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            uint32_t c = 0;
            const bool inBody = x >= 2 && x <= 29 && y >= 5 && y <= 23;
            const bool inScreen = x >= 5 && x <= 26 && y >= 8 && y <= 20;
            if (inBody) c = inScreen ? fill : frame;
            if (x >= 13 && x <= 18 && y >= 24 && y <= 26) c = frame;  // ayak
            if (x >= 8 && x <= 23 && y >= 27 && y <= 28) c = frame;   // taban
            px[y * S + x] = c;
        }
    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

// --- Tepsi ---

void updateTray() {
    g_nid.hIcon = g_connected ? g_iconOn : g_iconIdle;
    std::wstring tip = g_connected ? L"EKRAN — " + g_device + L" (" + g_link + L")" : L"EKRAN — tablet bekleniyor";
    if (g_connected && !g_stats.empty()) tip += L"\n" + g_stats;
    wcsncpy(g_nid.szTip, tip.c_str(), ARRAYSIZE(g_nid.szTip) - 1);
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void notify(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    wcsncpy(n.szInfoTitle, title.c_str(), ARRAYSIZE(n.szInfoTitle) - 1);
    wcsncpy(n.szInfo, text.c_str(), ARRAYSIZE(n.szInfo) - 1);
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

// Hizmet modunda: ajanın yazdığı durumu izle (kayıt defteri bildirimi; yoklama yok)
void startStatusWatcher() {
    g_statusQuit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_statusWatcher = std::thread([] {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, config::kStatusKey, 0, KEY_NOTIFY | KEY_QUERY_VALUE, &k) != 0) return;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        bool last = false;
        bool first = true;
        for (;;) {
            RegNotifyChangeKeyValue(k, FALSE, REG_NOTIFY_CHANGE_LAST_SET, ev, TRUE);
            bool c = false;
            std::wstring dev, link;
            if (config::readStatus(c, dev, link) && (first || c != last)) {
                auto narrow = [](const std::wstring& w) {
                    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
                    std::string s(size_t(n), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
                    return s;
                };
                if (!first || c) PostMessageW(g_wnd, WM_STATE, 0, reinterpret_cast<LPARAM>(new StateMsg{c, narrow(dev), narrow(link)}));
                last = c;
                first = false;
            }
            HANDLE hs[2] = {ev, g_statusQuit};
            if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) != WAIT_OBJECT_0) break;
        }
        CloseHandle(ev);
        RegCloseKey(k);
    });
}

void stopStatusWatcher() {
    if (!g_statusQuit) return;
    SetEvent(g_statusQuit);
    if (g_statusWatcher.joinable()) g_statusWatcher.join();
    CloseHandle(g_statusQuit);
    g_statusQuit = nullptr;
}

void startStreamer() {
    if (g_serviceMode) {
        startStatusWatcher();
        return;
    }
    StreamerEvents ev;
    ev.onState = [](bool connected, const std::string& device, const std::string& link) {
        PostMessageW(g_wnd, WM_STATE, 0, reinterpret_cast<LPARAM>(new StateMsg{connected, device, link}));
    };
    ev.onPairRequest = [](const std::string& device, const std::string& code, HANDLE cancel) {
        return askPairing(widen(device), widen(code), cancel);
    };    ev.onStats = [](const std::string& line) {
        PostMessageW(g_wnd, WM_STATS, 0, reinterpret_cast<LPARAM>(new std::string(line)));
    };
    g_streamer = std::make_unique<Streamer>(g_opt, ev);
    g_worker = std::thread([] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        g_streamer->run();
        CoUninitialize();
    });
}

void stopStreamer() {
    stopStatusWatcher();
    if (g_streamer) g_streamer->stop();
    if (g_worker.joinable()) g_worker.join();
    g_streamer.reset();
    g_connected = false;
}

void restartStreamer() {
    if (g_serviceMode) return;  // ajan kayıt defterindeki değişikliği görüp kendisi yeniden başlar
    stopStreamer();
    startStreamer();
    updateTray();
}

std::vector<std::wstring> listOutputs() {
    std::vector<std::wstring> out;
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&f)))) return out;
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
        IDXGIOutput* o = nullptr;
        for (UINT j = 0; a->EnumOutputs(j, &o) != DXGI_ERROR_NOT_FOUND; j++) {
            DXGI_OUTPUT_DESC d;
            o->GetDesc(&d);
            wchar_t s[128];
            swprintf(s, 128, L"Ekran %zu  (%ld×%ld)", out.size() + 1, d.DesktopCoordinates.right - d.DesktopCoordinates.left,
                     d.DesktopCoordinates.bottom - d.DesktopCoordinates.top);
            out.push_back(s);
            o->Release();
        }
        a->Release();
    }
    f->Release();
    return out;
}

void showMenu() {
    HMENU m = CreatePopupMenu();
    std::wstring status = g_connected ? L"Bağlı: " + g_device + L" (" + g_link + L")" : L"Tablet bekleniyor…";
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, status.c_str());
    if (g_connected && !g_stats.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, g_stats.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    HMENU link = CreatePopupMenu();
    AppendMenuW(link, MF_STRING, kMenuAuto, L"Otomatik (önce USB)");
    AppendMenuW(link, MF_STRING, kMenuUsb, L"Sadece USB");
    AppendMenuW(link, MF_STRING, kMenuWifi, L"Sadece Wi-Fi");
    const UINT sel = g_opt.mode == LinkMode::Usb ? kMenuUsb : g_opt.mode == LinkMode::Wifi ? kMenuWifi : kMenuAuto;
    CheckMenuRadioItem(link, kMenuAuto, kMenuWifi, sel, MF_BYCOMMAND);
    AppendMenuW(link, MF_SEPARATOR, 0, nullptr);
    // Tablet takılınca/bulununca kendiliğinden bağlan. Tabletin de kendi "otomatik aç" ayarı var: ikisi açıksa olur.
    AppendMenuW(link, MF_STRING | (g_opt.autoConnect ? MF_CHECKED : 0), kMenuAutoConnect,
                L"Otomatik bağlan (tablet takılınca)");
    AppendMenuW(link, MF_STRING | (g_opt.autoConnect || g_connected ? MF_GRAYED : 0), kMenuConnectNow, L"Şimdi bağlan");
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(link), L"Bağlantı");

    HMENU outs = CreatePopupMenu();
    auto names = listOutputs();
    for (size_t i = 0; i < names.size(); i++) AppendMenuW(outs, MF_STRING, kMenuOutputBase + i, names[i].c_str());
    if (!names.empty())
        CheckMenuRadioItem(outs, kMenuOutputBase, kMenuOutputBase + UINT(names.size()) - 1,
                           kMenuOutputBase + UINT(g_opt.output), MF_BYCOMMAND);
    HMENU disp = CreatePopupMenu();
    const bool driver = VirtualDisplay::driverAvailable();
    AppendMenuW(disp, MF_STRING | (driver ? 0 : MF_GRAYED), kMenuExtend,
                driver ? L"İkinci ekran (tablet ayrı monitör olur)" : L"İkinci ekran (sürücü kurulu değil)");
    AppendMenuW(disp, MF_STRING | (driver ? 0 : MF_GRAYED), kMenuOnly,
                L"Tek ekran (tablet bağlıyken diğer monitörler kapanır)");
    AppendMenuW(disp, MF_STRING, kMenuMirror, L"Yansıtma (seçili PC ekranının kopyası)");
    const UINT dsel = !g_opt.extend || !driver ? kMenuMirror : g_opt.onlyTablet ? kMenuOnly : kMenuExtend;
    CheckMenuRadioItem(disp, kMenuExtend, kMenuMirror, dsel, MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(disp), L"Görüntü");
    AppendMenuW(m, MF_POPUP | (g_opt.extend && driver ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(outs), L"Yansıtılan ekran");

    HMENU touch = CreatePopupMenu();
    AppendMenuW(touch, MF_STRING, kMenuTouch, L"Dokunmatik ekran (çoklu dokunma, hareketler)");
    AppendMenuW(touch, MF_STRING, kMenuMouse, L"Fare (tek parmak = sol tık)");
    CheckMenuRadioItem(touch, kMenuTouch, kMenuMouse, g_opt.touchAsMouse ? kMenuMouse : kMenuTouch, MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(touch), L"Dokunma");
    HMENU perf = CreatePopupMenu();
    AppendMenuW(perf, MF_STRING, kMenuBalanced, L"Dengeli (az işlemci, pil dostu)");
    AppendMenuW(perf, MF_STRING, kMenuLowLatency, L"Düşük gecikme (tablet daha çok çalışır)");
    CheckMenuRadioItem(perf, kMenuBalanced, kMenuLowLatency, g_opt.lowLatency ? kMenuLowLatency : kMenuBalanced, MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(perf), L"Performans");
    AppendMenuW(m, MF_STRING, kMenuInstall, L"Tablete uygulamayı kur…");
    AppendMenuW(m, MF_STRING | (autostartEnabled() ? MF_CHECKED : 0), kMenuAutostart, L"Windows ile başlat");
    {
        const bool stale = g_serviceMode && service::installedVersion() != config::kVersion;
        std::wstring label = L"Oturum açılmadan önce başlat (kilit ekranı, UAC)";
        if (stale) label += L" — güncelle";
        AppendMenuW(m, MF_STRING | (g_serviceMode ? MF_CHECKED : 0), kMenuService, label.c_str());
    }
    AppendMenuW(m, MF_STRING, kMenuForget, L"Eşleşmeleri unut");
    AppendMenuW(m, MF_STRING, kMenuLog, L"Günlüğü aç");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuExit, L"Çıkış");

    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g_wnd);  // menü dışına tıklanınca kapanması için gerekli
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, g_wnd, nullptr);
    DestroyMenu(m);
}

void onCommand(UINT id) {
    switch (id) {
        case kMenuAuto:
        case kMenuUsb:
        case kMenuWifi:
            g_opt.mode = id == kMenuUsb ? LinkMode::Usb : id == kMenuWifi ? LinkMode::Wifi : LinkMode::Auto;
            regSet(L"Mode", DWORD(g_opt.mode));
            if (g_streamer) g_streamer->setMode(g_opt.mode);
            break;
        case kMenuAutoConnect:
            g_opt.autoConnect = !g_opt.autoConnect;
            regSet(L"AutoConnect", g_opt.autoConnect ? 1 : 0);
            restartStreamer();
            break;
        case kMenuConnectNow:
            if (g_serviceMode) config::set(L"ConnectNow", config::get(L"ConnectNow", 0) + 1);  // ajan izliyor
            else if (g_streamer) g_streamer->connectNow();
            break;
        case kMenuAutostart:
            setAutostart(!autostartEnabled());
            break;
        case kMenuInstall: {
            const std::wstring apk = adb::bundledApk();
            if (apk.empty() || !g_apkServer.start(apk, ApkServer::kPort, 10)) {
                MessageBoxW(nullptr, L"Uygulama dosyası (ekran-android.apk) bulunamadı ya da sunulamadı.\nGünlüğe bakın.",
                            L"EKRAN", MB_OK | MB_ICONWARNING);
                break;
            }
            std::wstring text =
                L"1) Tableti bu bilgisayarla aynı Wi-Fi ağına bağlayın.\n"
                L"2) Tablette tarayıcıyı açıp şu adrese gidin:\n\n";
            for (const auto& u : g_apkServer.urls()) text += L"        " + widen(u) + L"\n";
            text += L"\n3) Sayfadaki talimatla EKRAN'ı indirip kurun.\n\n"
                    L"Sayfa 10 dakika açık kalır.\n"
                    L"USB hata ayıklaması açık bir tableti USB ile takarsanız EKRAN kendiliğinden kurulur.";
            // Pencere menü döngüsünü bekletmesin
            std::thread([text] { MessageBoxW(nullptr, text.c_str(), L"EKRAN — tablete kur", MB_OK | MB_ICONINFORMATION); }).detach();
            break;
        }
        case kMenuExtend:
        case kMenuOnly:
        case kMenuMirror:
            g_opt.extend = id != kMenuMirror;
            g_opt.onlyTablet = id == kMenuOnly;
            regSet(L"Mirror", g_opt.extend ? 0 : 1);
            regSet(L"OnlyTablet", g_opt.onlyTablet ? 1 : 0);
            restartStreamer();
            break;
        case kMenuTouch:
        case kMenuMouse:
            g_opt.touchAsMouse = id == kMenuMouse;
            regSet(L"TouchAsMouse", g_opt.touchAsMouse ? 1 : 0);
            restartStreamer();
            break;
        case kMenuBalanced:
        case kMenuLowLatency:
            g_opt.lowLatency = id == kMenuLowLatency;
            regSet(L"LowLatency", g_opt.lowLatency ? 1 : 0);
            restartStreamer();
            break;
        case kMenuForget:
            if (MessageBoxW(nullptr, L"Tüm eşleşmiş tabletler unutulsun mu?\nWi-Fi ile bağlanırken yeniden kod onayı istenir.",
                            L"EKRAN", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (g_serviceMode) {
                    config::set(L"ForgetPairs", 1);  // eşleşmeleri hizmet tutar (kullanıcı okuyamaz)
                } else {
                    pairing::forgetAll();
                    logf("Tüm eşleşmeler silindi");
                }
            }
            break;
        case kMenuLog:
            ShellExecuteW(nullptr, L"open", (g_serviceMode ? service::agentLogPath() : g_logPath).c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
            break;
        case kMenuService: {
            const bool stale = g_serviceMode && service::installedVersion() != config::kVersion;
            const bool remove = g_serviceMode && !stale;
            if (remove && MessageBoxW(nullptr,
                                      L"EKRAN hizmeti kaldırılsın mı?\n\nOturum açma ekranı ve UAC pencereleri artık "
                                      L"tablette görünmez; yayını yine bu uygulama yapar.",
                                      L"EKRAN", MB_YESNO | MB_ICONQUESTION) != IDYES)
                break;
            // Kurulumda tablete aynı anda iki yayıncı bağlanmasın: önce kendi yayınımızı durdur
            stopStreamer();
            DWORD code = 1;
            const bool ran = service::runElevated(remove ? L"--hizmet-kaldir" : L"--hizmet-kur", code);
            g_serviceMode = service::installed();
            config::useMachineStore(g_serviceMode);
            g_opt = config::loadOptions();
            if (!ran) {
                logf("Hizmet işlemi iptal edildi");
            } else if (code != 0) {
                logf("Hizmet işlemi başarısız (%lu)", code);
                MessageBoxW(nullptr, L"Hizmet kurulamadı/kaldırılamadı. Ayrıntı: C:\\ProgramData\\EKRAN\\hizmet.log",
                            L"EKRAN", MB_OK | MB_ICONWARNING);
            } else {
                logf(g_serviceMode ? "Hizmet kuruldu: yayını artık hizmet yapıyor" : "Hizmet kaldırıldı");
                notify(L"EKRAN", g_serviceMode ? L"Tablet artık oturum açma ekranını da gösterecek."
                                               : L"Hizmet kaldırıldı; yayını bu uygulama yapıyor.");
            }
            startStreamer();
            updateTray();
            break;
        }
        case kMenuExit:
            DestroyWindow(g_wnd);
            break;
        default:
            if (id >= kMenuOutputBase && id < kMenuOutputBase + 16) {
                g_opt.output = int(id - kMenuOutputBase);
                regSet(L"Output", DWORD(g_opt.output));
                restartStreamer();
            }
    }
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    switch (msg) {
        case WM_TRAY:
            if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_LBUTTONUP) showMenu();
            return 0;
        case WM_COMMAND:
            onCommand(LOWORD(w));
            return 0;
        case WM_STATE: {
            std::unique_ptr<StateMsg> s(reinterpret_cast<StateMsg*>(l));
            g_connected = s->connected;
            g_device = widen(s->device);
            g_link = widen(s->link);
            g_stats.clear();
            updateTray();
            notify(g_connected ? L"Tablet bağlandı" : L"Tablet ayrıldı", g_device + L" (" + g_link + L")");
            return 0;
        }
        case WM_STATS: {
            std::unique_ptr<std::string> s(reinterpret_cast<std::string*>(l));
            // Tepsi ipucu için kısa özet: "PC→çizim 21.5 ms · 7.3 Mbit/s"
            std::wstring full = widen(*s);
            size_t lat = full.find(L"PC→çizim");
            size_t mb = full.find(L"Mbit/s");
            g_stats.clear();
            if (lat != std::wstring::npos) g_stats = full.substr(lat, full.find(L"ms", lat) + 2 - lat);
            if (mb != std::wstring::npos) {
                size_t b = full.rfind(L"  ", mb);
                g_stats += L" · " + full.substr(b + 2, mb + 6 - b - 2);
            }
            updateTray();
            return 0;
        }
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        default:
            if (msg == taskbarCreated) Shell_NotifyIconW(NIM_ADD, &g_nid);  // Gezgin yeniden başladı
            return DefWindowProcW(h, msg, w, l);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    // Aynı exe'nin diğer rolleri (service.h)
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        const std::wstring role = argc > 1 ? argv[1] : L"";
        LocalFree(argv);
        if (role == L"--hizmet") return service::runService();
        if (role == L"--ajan") return service::runAgent();
        if (role == L"--hizmet-kur") return service::install();
        if (role == L"--hizmet-kaldir") return service::uninstall();
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"EKRAN-tepsi");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // zaten çalışıyor

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    openLog();
    logf("EKRAN tepsi uygulaması başladı");

    g_serviceMode = service::installed();
    config::useMachineStore(g_serviceMode);
    g_opt = config::loadOptions();
    if (g_serviceMode) logf("EKRAN hizmeti kurulu: yayını hizmet yapıyor, bu uygulama yalnızca ayar ve durum");

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"EkranTray";
    RegisterClassW(&wc);
    g_wnd = CreateWindowW(wc.lpszClassName, L"EKRAN", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);

    g_iconIdle = makeIcon(RGB(110, 110, 110));
    g_iconOn = makeIcon(RGB(40, 160, 90));
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_wnd;
    g_nid.uID = kTrayId;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_iconIdle;
    wcscpy(g_nid.szTip, L"EKRAN — tablet bekleniyor");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    startStreamer();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    stopStreamer();
    g_apkServer.stop();
    logf("EKRAN kapandı");
    WSACleanup();
    MFShutdown();
    CoUninitialize();
    CloseHandle(single);
    return 0;
}
