// EkranLink — bildirim alanı (tepsi) uygulaması. Saatin yanındaki simge: sol tık ayar penceresini açar,
// sağ tık kısa menü. Boştayken hiçbir şey yapmaz: tablet yoklaması 3 saniyede bir, yayın yalnızca bağlıyken;
// ayar penceresi yalnızca açıkken var olur.

#include <winsock2.h>
#include <windows.h>
#include <dxgi.h>
#include <mfapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "i18n.h"
#include "log.h"
#include "pair_prompt.h"
#include "pairing.h"
#include "service.h"
#include "streamer.h"
#include "ui.h"
#include "virtual_display.h"

using namespace ekran;
using i18n::W;

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_STATE = WM_APP + 2;     // lParam: SessionState*
constexpr UINT WM_STATS = WM_APP + 3;     // lParam: std::string*
constexpr UINT WM_NOTICE = WM_APP + 4;    // wParam: Notice, lParam: std::string* (ek bilgi)
constexpr UINT WM_OPEN_UI = WM_APP + 5;   // ikinci kez çalıştırıldı: pencereyi aç
constexpr UINT WM_SETTINGS = WM_APP + 6;  // yön / çözünürlük Windows ayarlarından değişip kaydedildi
constexpr UINT kTrayId = 1;

enum MenuId : UINT {
    kMenuOpen = 100,
    kMenuConnectNow,
    kMenuLog,
    kMenuExit,
};

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kBackgroundArg = L"--arka-plan";  // Windows ile başlarken: pencere açılmaz

HWND g_wnd = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_iconIdle = nullptr, g_iconOn = nullptr;
Options g_opt;
std::unique_ptr<Streamer> g_streamer;
std::thread g_worker;
bool g_connected = false;
std::wstring g_device, g_link;
std::wstring g_latency, g_data, g_fps;  // son ölçüm (bağlıyken)
DisplayMode g_mode = DisplayMode::Extend;
int g_width = 0, g_height = 0;  // bağlı cihazın ekranı (önerilen çözünürlük)
std::string g_deviceId;
std::wstring g_gdi;              // ikinci ekranda sanal monitörün Windows adı
std::vector<ui::Resolution> g_modes;
ui::Resolution g_current;
int g_rotation = 0;
Notice g_notice = Notice(0);     // çözülene kadar pencerede kalan uyarı
std::wstring g_noticeArg;
std::wstring g_logPath;
// Hizmet kuruluysa yayını hizmet (ajan) yapar; tepsi yalnızca arayüzdür: ayarları HKLM'e yazar,
// durumu oradan okur. Değilse yayını tepsi kendisi yapar (ayarlar HKCU).
bool g_serviceMode = false;
HANDLE g_statusQuit = nullptr;
std::thread g_statusWatcher;
// Pencere için önbellek (pencere açılınca ve ekranlar değişince tazelenir)
std::vector<std::wstring> g_outputs;
bool g_driver = false, g_serviceStale = false;

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
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"EkranLink", RRF_RT_REG_SZ, nullptr, v, &size) == 0;
}

void setAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != 0) return;
    if (on) {
        std::wstring cmd = L"\"" + exePath() + L"\" " + kBackgroundArg;
        RegSetValueExW(k, L"EkranLink", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"EkranLink");
    }
    RegCloseKey(k);
}

// Dil: ayar yoksa Windows'un dili. Hizmet (SYSTEM) kullanıcının dilini bilemez: çözülmüş dili ona da yaz.
void applyLanguage() {
    const int setting = int(config::get(L"Language", 0));
    i18n::setLanguage(setting);
    if (g_serviceMode) {
        const DWORD ui = i18n::systemLanguage() == i18n::Lang::Tr ? 1 : 2;
        if (config::get(L"UiLanguage", 0) != ui) config::set(L"UiLanguage", ui);
    }
}

// --- Günlük dosyası ---

void openLog() {
    wchar_t dir[MAX_PATH];
    GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    std::wstring d = std::wstring(dir) + L"\\EkranLink";
    CreateDirectoryW(d.c_str(), nullptr);
    g_logPath = d + L"\\ekranlink.log";
    logToFile(g_logPath);
}

// --- Simge: res/app.rc (1: renkli, bağlıyken; 2: gri, bağlı değilken). Kaynak çizim: tools/simge.py ---

HICON loadIcon(int id, int size) {
    return static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
}

// --- Tepsi ---

void updateTray() {
    g_nid.hIcon = g_connected ? g_iconOn : g_iconIdle;
    std::wstring tip = g_connected ? L"EkranLink — " + g_device + L" (" + g_link + L")"
                                   : std::wstring(L"EkranLink — ") + W(i18n::Waiting);
    if (g_connected && !g_latency.empty()) tip += L"\n" + std::wstring(W(i18n::Latency)) + L" " + g_latency + L" · " + g_data;
    wcsncpy(g_nid.szTip, tip.c_str(), ARRAYSIZE(g_nid.szTip) - 1);
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    ui::refresh();
}

void notify(const std::wstring& title, const std::wstring& text, DWORD icon = NIIF_INFO) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    wcsncpy(n.szInfoTitle, title.c_str(), ARRAYSIZE(n.szInfoTitle) - 1);
    wcsncpy(n.szInfo, text.c_str(), ARRAYSIZE(n.szInfo) - 1);
    n.dwInfoFlags = icon | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void showNotice(Notice code, const std::string& arg) {
    switch (code) {
        case Notice::AppMissing:
            notify(W(i18n::NoticeAppMissingTitle), W(i18n::NoticeAppMissing), NIIF_WARNING);
            break;
        case Notice::TabletOutdated:
            notify(W(i18n::NoticeTabletOldTitle), W(i18n::NoticeTabletOld), NIIF_WARNING);
            break;
        case Notice::PcOutdated:
            notify(W(i18n::NoticePcOldTitle), i18n::F(i18n::NoticePcOld, widen(arg).c_str()), NIIF_WARNING);
            break;
    }
}

// Hizmet modunda: ajanın yazdığı durumu izle (kayıt defteri bildirimi; yoklama yok)
void startStatusWatcher() {
    g_statusQuit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_statusWatcher = std::thread([] {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, config::kStatusKey, 0, KEY_NOTIFY | KEY_QUERY_VALUE, &k) != 0) return;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        DWORD lastSeq = 0;
        bool first = true;
        std::string lastStats;
        DWORD lastNotice = config::noticeId();  // açılıştan önceki uyarılar gösterilmez
        for (;;) {
            RegNotifyChangeKeyValue(k, FALSE, REG_NOTIFY_CHANGE_LAST_SET, ev, TRUE);
            if (config::noticeId() != lastNotice) {
                int code = 0;
                auto* arg = new std::string;
                lastNotice = config::readNotice(code, *arg);
                PostMessageW(g_wnd, WM_NOTICE, WPARAM(code), reinterpret_cast<LPARAM>(arg));
            }
            const std::string stats = config::readStats();
            if (stats != lastStats) {
                lastStats = stats;
                if (!stats.empty()) PostMessageW(g_wnd, WM_STATS, 0, reinterpret_cast<LPARAM>(new std::string(stats)));
            }
            const DWORD seq = config::statusSeq();
            SessionState st;
            if ((first || seq != lastSeq) && config::readStatus(st)) {
                if (!first || st.connected) PostMessageW(g_wnd, WM_STATE, 0, reinterpret_cast<LPARAM>(new SessionState(st)));
                lastSeq = seq;
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
    ev.onState = [](const SessionState& s) {
        PostMessageW(g_wnd, WM_STATE, 0, reinterpret_cast<LPARAM>(new SessionState(s)));
    };
    ev.onPairRequest = [](const std::string& device, const std::string& code, HANDLE cancel) {
        return askPairing(widen(device), widen(code), cancel);
    };
    ev.onStats = [](const std::string& line) {
        PostMessageW(g_wnd, WM_STATS, 0, reinterpret_cast<LPARAM>(new std::string(line)));
    };
    ev.onNotice = [](Notice n, const std::string& arg) {
        PostMessageW(g_wnd, WM_NOTICE, WPARAM(n), reinterpret_cast<LPARAM>(new std::string(arg)));
    };
    ev.onSettingsChanged = [] { PostMessageW(g_wnd, WM_SETTINGS, 0, 0); };
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

// Ayar değişti: bağlantı kopmadan uygulanır (hizmet modunda ajan kayıt defterindeki değişikliği görür)
void applyOptions() {
    if (g_streamer) g_streamer->update(g_opt);
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
            out.push_back(i18n::F(i18n::ScreenN, int(out.size() + 1),
                                  int(d.DesktopCoordinates.right - d.DesktopCoordinates.left),
                                  int(d.DesktopCoordinates.bottom - d.DesktopCoordinates.top)));
            o->Release();
        }
        a->Release();
    }
    f->Release();
    return out;
}

// Sanal monitörün Windows'taki modları: Windows ekran ayarlarındaki listenin aynısı (yatay yöndeki boyutlarla)
void readDisplayModes() {
    g_modes.clear();
    g_current = {};
    g_rotation = 0;
    if (!g_connected || g_gdi.empty()) return;
    DEVMODEW cur{};
    cur.dmSize = sizeof cur;
    if (!EnumDisplaySettingsExW(g_gdi.c_str(), ENUM_CURRENT_SETTINGS, &cur, 0)) return;
    const bool portrait = cur.dmDisplayOrientation & 1;
    g_rotation = int(cur.dmDisplayOrientation) * 90;
    auto land = [portrait](const DEVMODEW& d) {
        return portrait ? ui::Resolution{int(d.dmPelsHeight), int(d.dmPelsWidth)}
                        : ui::Resolution{int(d.dmPelsWidth), int(d.dmPelsHeight)};
    };
    g_current = land(cur);
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    for (DWORD i = 0; EnumDisplaySettingsExW(g_gdi.c_str(), i, &dm, 0); i++) {
        if (dm.dmBitsPerPel != cur.dmBitsPerPel) continue;
        const ui::Resolution r = land(dm);
        bool dup = false;
        for (const auto& e : g_modes) dup |= e.width == r.width && e.height == r.height;
        if (!dup) g_modes.push_back(r);
    }
    std::sort(g_modes.begin(), g_modes.end(), [](const ui::Resolution& a, const ui::Resolution& b) {
        return a.width != b.width ? a.width > b.width : a.height > b.height;
    });
}

// Pencerenin gösterdiği, sık değişmeyen bilgiler
void reloadCaches() {
    applyLanguage();  // dil başka yerden (ör. hizmet kurulumu) değişmiş olabilir
    readDisplayModes();
    g_opt = config::loadOptions();
    g_outputs = listOutputs();
    g_driver = VirtualDisplay::driverAvailable();
    g_serviceStale = g_serviceMode && service::installedVersion() != config::kVersion;
}

void installOrRemoveService(bool update) {
    const bool remove = g_serviceMode && !g_serviceStale && !update;
    if (remove && MessageBoxW(ui::window(), W(i18n::ServiceRemoveConfirm), L"EkranLink", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    // Kurulumda tablete aynı anda iki yayıncı bağlanmasın: önce kendi yayınımızı durdur
    stopStreamer();
    DWORD code = 1;
    const bool ran = service::runElevated(remove ? L"--hizmet-kaldir" : L"--hizmet-kur", code);
    g_serviceMode = service::installed();
    config::useMachineStore(g_serviceMode);
    applyLanguage();
    reloadCaches();
    if (!ran) {
        logf("Hizmet işlemi iptal edildi");
    } else if (code != 0) {
        logf("Hizmet işlemi başarısız (%lu)", code);
        MessageBoxW(ui::window(), W(i18n::ServiceFailed), L"EkranLink", MB_OK | MB_ICONWARNING);
    } else {
        logf(g_serviceMode ? "Hizmet kuruldu: yayını artık hizmet yapıyor" : "Hizmet kaldırıldı");
        notify(L"EkranLink", W(g_serviceMode ? i18n::ServiceInstalled : i18n::ServiceRemoved));
    }
    startStreamer();
    updateTray();
}

// --- Ayar penceresiyle bağlantı ---

class AppHost : public ui::Host {
public:
    ui::Model model() override {
        ui::Model m;
        m.opt = g_opt;
        m.connected = g_connected;
        m.device = g_device;
        m.link = g_link;
        m.latency = g_latency;
        m.data = g_data;
        m.fps = g_fps;
        m.activeMode = g_mode;
        m.deviceWidth = g_width;
        m.deviceHeight = g_height;
        m.modes = g_modes;
        m.current = g_current;
        m.currentRotation = g_rotation;
        m.notice = g_notice;
        m.noticeArg = g_noticeArg;
        m.driver = g_driver;
        m.serviceMode = g_serviceMode;
        m.serviceStale = g_serviceStale;
        m.autostart = autostartEnabled();
        m.language = int(config::get(L"Language", 0));
        m.outputs = g_outputs;
        return m;
    }

    void act(ui::Act a, int v) override {
        switch (a) {
            case ui::Act::Reload:
                reloadCaches();
                break;
            case ui::Act::Closed:
                if (g_serviceMode) config::set(L"Izle", 0);  // ajan ölçüm yazmayı bıraksın
                return;
            case ui::Act::OpenDisplaySettings:
                ShellExecuteW(nullptr, L"open", L"ms-settings:display", nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case ui::Act::Link:
                g_opt.mode = LinkMode(v);
                regSet(L"Mode", DWORD(v));
                applyOptions();
                break;
            case ui::Act::AutoConnect:
                g_opt.autoConnect = v != 0;
                regSet(L"AutoConnect", DWORD(v));
                applyOptions();
                break;
            case ui::Act::ConnectNow:
                if (g_serviceMode) config::set(L"ConnectNow", config::get(L"ConnectNow", 0) + 1);  // ajan izliyor
                else if (g_streamer) g_streamer->connectNow();
                break;
            case ui::Act::Display:
                g_opt.extend = v != int(DisplayMode::Mirror);
                g_opt.onlyTablet = v == int(DisplayMode::Only);
                regSet(L"Mirror", g_opt.extend ? 0 : 1);
                regSet(L"OnlyTablet", g_opt.onlyTablet ? 1 : 0);
                applyOptions();
                break;
            case ui::Act::Output:
                g_opt.output = v;
                regSet(L"Output", DWORD(v));
                applyOptions();
                break;
            case ui::Act::Rotation:
                g_opt.rotation = v;
                config::saveRotation(v);
                applyOptions();
                break;
            case ui::Act::Resolution:
                // Cihaza özel kaydedilir; bağlıyken hemen uygulanır (ajan / yayın değişikliği görür)
                if (v >= 0 && v < int(g_modes.size()) && !g_deviceId.empty()) {
                    config::saveResolution(g_deviceId, g_modes[size_t(v)].width, g_modes[size_t(v)].height);
                    applyOptions();
                }
                break;
            case ui::Act::Touch:
                g_opt.touchAsMouse = v != 0;
                regSet(L"TouchAsMouse", DWORD(v));
                applyOptions();
                break;
            case ui::Act::LowLatency:
                g_opt.lowLatency = v != 0;
                regSet(L"LowLatency", DWORD(v));
                applyOptions();
                break;
            case ui::Act::Autostart:
                setAutostart(v != 0);
                break;
            case ui::Act::Service:
                installOrRemoveService(v != 0);
                break;
            case ui::Act::Language:
                regSet(L"Language", DWORD(v));
                applyLanguage();
                g_outputs = listOutputs();
                break;
            case ui::Act::Forget:
                if (MessageBoxW(ui::window(), W(i18n::ForgetConfirm), L"EkranLink", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (g_serviceMode) {
                        config::set(L"ForgetPairs", 1);  // eşleşmeleri hizmet tutar (kullanıcı okuyamaz)
                    } else {
                        pairing::forgetAll();
                        logf("Tüm eşleşmeler silindi");
                    }
                }
                break;
            case ui::Act::OpenLog:
                ShellExecuteW(nullptr, L"open", (g_serviceMode ? service::agentLogPath() : g_logPath).c_str(), nullptr,
                              nullptr, SW_SHOWNORMAL);
                break;
        }
        updateTray();
    }
};

AppHost g_host;

void openUi() {
    if (g_serviceMode && !ui::window()) config::set(L"Izle", 1);  // pencere açıkken ajan ölçümleri yazsın
    reloadCaches();
    ui::show(&g_host, g_connected ? g_iconOn : g_iconIdle);
}

void showMenu() {
    HMENU m = CreatePopupMenu();
    std::wstring status = g_connected ? std::wstring(W(i18n::Connected)) + L": " + g_device + L" (" + g_link + L")"
                                      : W(i18n::Waiting);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, status.c_str());
    if (g_connected && !g_latency.empty()) {
        const std::wstring stats = std::wstring(W(i18n::Latency)) + L" " + g_latency + L" · " + g_data;
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, stats.c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | MF_DEFAULT, kMenuOpen, W(i18n::MenuOpen));
    if (!g_connected && !g_opt.autoConnect) AppendMenuW(m, MF_STRING, kMenuConnectNow, W(i18n::ConnectNow));
    AppendMenuW(m, MF_STRING, kMenuLog, W(i18n::OpenLog));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuExit, W(i18n::MenuExit));

    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g_wnd);  // menü dışına tıklanınca kapanması için gerekli
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, g_wnd, nullptr);
    DestroyMenu(m);
}

void onCommand(UINT id) {
    switch (id) {
        case kMenuOpen:
            openUi();
            break;
        case kMenuConnectNow:
            g_host.act(ui::Act::ConnectNow, 0);
            break;
        case kMenuLog:
            g_host.act(ui::Act::OpenLog, 0);
            break;
        case kMenuExit:
            ui::close();
            DestroyWindow(g_wnd);
            break;
    }
}

// Ölçüm satırı (streamer.cpp): "%3d kare +%3d itici  %6.2f Mbit/s  RTT %5.1f ms  PC→çizim %5.1f ms  ..."
void parseStats(const std::string& line) {
    int frames = 0, pushers = 0;
    float mbps = 0, rtt = 0, e2e = 0;
    if (std::sscanf(line.c_str(), "%d kare +%d itici %f Mbit/s RTT %f ms", &frames, &pushers, &mbps, &rtt) != 4) return;
    const size_t p = line.find("PC→çizim");
    if (p == std::string::npos || std::sscanf(line.c_str() + p + std::strlen("PC→çizim"), "%f", &e2e) != 1) return;
    wchar_t buf[64];
    if (e2e > 0) {
        swprintf(buf, 64, L"%.0f ms", double(e2e));
        g_latency = buf;
    } else {
        g_latency.clear();  // ekran değişmiyorsa ölçülecek kare yok
    }
    swprintf(buf, 64, L"%.1f Mbit/s", double(mbps));
    g_data = buf;
    swprintf(buf, 64, L"%d fps", frames);
    g_fps = buf;
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    switch (msg) {
        case WM_TRAY:
            if (LOWORD(l) == WM_LBUTTONUP) openUi();
            else if (LOWORD(l) == WM_RBUTTONUP) showMenu();
            return 0;
        case WM_OPEN_UI:
            openUi();
            return 0;
        case WM_COMMAND:
            onCommand(LOWORD(w));
            return 0;
        case WM_STATE: {
            std::unique_ptr<SessionState> s(reinterpret_cast<SessionState*>(l));
            const bool changed = s->connected != g_connected;
            g_connected = s->connected;
            g_device = widen(s->device);
            g_link = widen(s->link);
            g_mode = s->mode;
            g_width = s->width;
            g_height = s->height;
            g_deviceId = s->deviceId;
            g_gdi = s->gdiName;
            if (changed) {
                g_latency.clear();
                g_data.clear();
                g_fps.clear();
                if (g_connected) g_notice = Notice(0);  // bağlandı: uyarının konusu çözüldü
                notify(W(g_connected ? i18n::NotifyConnected : i18n::NotifyDisconnected), g_device + L" (" + g_link + L")");
            }
            readDisplayModes();
            updateTray();
            return 0;
        }
        case WM_NOTICE: {
            std::unique_ptr<std::string> arg(reinterpret_cast<std::string*>(l));
            g_notice = Notice(w);
            g_noticeArg = widen(*arg);
            showNotice(g_notice, *arg);
            updateTray();
            return 0;
        }
        case WM_SETTINGS:
            g_opt = config::loadOptions();
            readDisplayModes();
            ui::refresh();
            return 0;
        case WM_STATS: {
            std::unique_ptr<std::string> s(reinterpret_cast<std::string*>(l));
            if (g_connected) parseStats(*s);
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
    bool background = false;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        const std::wstring role = argc > 1 ? argv[1] : L"";
        LocalFree(argv);
        if (role == L"--hizmet") return service::runService();
        if (role == L"--ajan") return service::runAgent();
        if (role == L"--hizmet-kur") return service::install();
        if (role == L"--hizmet-kaldir") return service::uninstall();
        background = role == kBackgroundArg;
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"EkranLink-tepsi");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Zaten çalışıyor: onun penceresini aç
        if (HWND other = FindWindowExW(HWND_MESSAGE, nullptr, L"EkranLinkTray", nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, WM_OPEN_UI, 0, 0);
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    openLog();
    logf("EkranLink tepsi uygulaması başladı");

    g_serviceMode = service::installed();
    config::useMachineStore(g_serviceMode);
    applyLanguage();
    g_opt = config::loadOptions();
    if (g_serviceMode) logf("EkranLink hizmeti kurulu: yayını hizmet yapıyor, bu uygulama yalnızca ayar ve durum");
    if (autostartEnabled()) setAutostart(true);  // eski kayıtta pencereyi açmama bayrağı yok

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"EkranLinkTray";
    RegisterClassW(&wc);
    g_wnd = CreateWindowW(wc.lpszClassName, L"EkranLink", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);

    g_iconIdle = loadIcon(2, GetSystemMetrics(SM_CXSMICON));
    g_iconOn = loadIcon(1, GetSystemMetrics(SM_CXSMICON));
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_wnd;
    g_nid.uID = kTrayId;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_iconIdle;
    wcsncpy(g_nid.szTip, (std::wstring(L"EkranLink — ") + W(i18n::Waiting)).c_str(), ARRAYSIZE(g_nid.szTip) - 1);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Önceki tepsi pencere açıkken kapanmış (çökmüş) olabilir: ajan boşuna ölçüm yazmasın
    if (g_serviceMode && config::get(L"Izle", 0)) config::set(L"Izle", 0);
    startStreamer();
    if (!background) openUi();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    stopStreamer();
    if (g_serviceMode) config::set(L"Izle", 0);
    logf("EkranLink kapandı");
    WSACleanup();
    MFShutdown();
    CoUninitialize();
    CloseHandle(single);
    return 0;
}
