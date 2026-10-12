// EkranLink — bildirim alanı (tepsi) uygulaması. Saatin yanındaki simge: sol tık ayar penceresini açar,
// sağ tık kısa menü (cihaz başına bağlan / bağlantıyı kes). Boştayken hiçbir şey yapmaz: cihaz araması yalnızca
// otomatik bağlanma açıkken ya da pencere açıkken, yayın yalnızca bağlıyken; ayar penceresi yalnızca açıkken var olur.

#include <winsock2.h>
#include <windows.h>
#include <dxgi.h>
#include <mfapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
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
constexpr UINT WM_DEVICES = WM_APP + 2;   // lParam: std::vector<DeviceInfo>*
constexpr UINT WM_STATS = WM_APP + 3;     // lParam: std::pair<std::string, std::string>* (cihaz, satır)
constexpr UINT WM_NOTICE = WM_APP + 4;    // wParam: Notice, lParam: std::string* (ek bilgi)
constexpr UINT WM_OPEN_UI = WM_APP + 5;   // ikinci kez çalıştırıldı: pencereyi aç
constexpr UINT WM_SETTINGS = WM_APP + 6;  // yön / çözünürlük Windows ayarlarından değişip kaydedildi
constexpr UINT kTrayId = 1;

enum MenuId : UINT {
    kMenuOpen = 100,
    kMenuLog,
    kMenuExit,
    kMenuDisconnectAll,
    kMenuDisconnect = 200,  // + cihaz sırası
    kMenuConnect = 300,     // + cihaz sırası
};

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kBackgroundArg = L"--arka-plan";  // Windows ile başlarken: pencere açılmaz

HWND g_wnd = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_iconIdle = nullptr, g_iconOn = nullptr;
Options g_opt;
std::unique_ptr<Streamer> g_streamer;
std::thread g_worker;
std::vector<DeviceInfo> g_devices;  // bağlılar önce
struct Measure {
    std::wstring latency, data, fps;
};
std::map<std::string, Measure> g_stats;  // bağlı cihazların son ölçümü (cihaz anahtarı)
std::string g_displayKey;                // Görüntü sayfasında ayarları gösterilen cihaz
std::vector<std::string> g_viewKeys;     // pencereye ve menüye verilen sıra (seçim → cihaz)
std::vector<ui::Resolution> g_modes;     // o cihazın monitörünün Windows'taki modları
Notice g_notice = Notice(0);             // çözülene kadar pencerede kalan uyarı
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

int connectedCount() {
    return int(std::count_if(g_devices.begin(), g_devices.end(),
                             [](const DeviceInfo& d) { return d.state == DeviceState::Connected; }));
}

const DeviceInfo* deviceByKey(const std::string& key) {
    for (const auto& d : g_devices)
        if (d.key == key) return &d;
    return nullptr;
}

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
    const int n = connectedCount();
    g_nid.hIcon = n ? g_iconOn : g_iconIdle;
    std::wstring tip = L"EkranLink — ";
    if (n == 0) {
        tip += W(i18n::Waiting);
    } else if (n > 1) {
        tip += i18n::F(i18n::DevicesConnected, n);
    } else {
        const DeviceInfo& d = g_devices.front();
        tip += widen(d.name) + L" (" + widen(d.link) + L")";
        auto it = g_stats.find(d.key);
        if (it != g_stats.end() && !it->second.latency.empty())
            tip += L"\n" + std::wstring(W(i18n::Latency)) + L" " + it->second.latency + L" · " + it->second.data;
    }
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
        std::map<std::string, std::string> lastStats;
        DWORD lastNotice = config::noticeId();  // açılıştan önceki uyarılar gösterilmez
        for (;;) {
            RegNotifyChangeKeyValue(k, FALSE, REG_NOTIFY_CHANGE_LAST_SET, ev, TRUE);
            if (config::noticeId() != lastNotice) {
                int code = 0;
                auto* arg = new std::string;
                lastNotice = config::readNotice(code, *arg);
                PostMessageW(g_wnd, WM_NOTICE, WPARAM(code), reinterpret_cast<LPARAM>(arg));
            }
            const DWORD seq = config::statusSeq();
            if (first || seq != lastSeq) {
                auto* list = new std::vector<DeviceInfo>;
                if (config::readDevices(*list)) {
                    PostMessageW(g_wnd, WM_DEVICES, 0, reinterpret_cast<LPARAM>(list));
                    lastSeq = seq;
                    first = false;
                } else {
                    delete list;
                }
            }
            for (const auto& [key, line] : config::readStats()) {
                if (lastStats[key] == line) continue;
                lastStats[key] = line;
                PostMessageW(g_wnd, WM_STATS, 0, reinterpret_cast<LPARAM>(new std::pair<std::string, std::string>(key, line)));
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
    ev.onDevices = [](const std::vector<DeviceInfo>& list) {
        PostMessageW(g_wnd, WM_DEVICES, 0, reinterpret_cast<LPARAM>(new std::vector<DeviceInfo>(list)));
    };
    ev.onPairRequest = [](const std::string& device, const std::string& code, HANDLE cancel) {
        return askPairing(widen(device), widen(code), cancel);
    };
    ev.onStats = [](const std::string& key, const std::string& line) {
        PostMessageW(g_wnd, WM_STATS, 0, reinterpret_cast<LPARAM>(new std::pair<std::string, std::string>(key, line)));
    };
    ev.onNotice = [](Notice n, const std::string& arg) {
        PostMessageW(g_wnd, WM_NOTICE, WPARAM(n), reinterpret_cast<LPARAM>(new std::string(arg)));
    };
    ev.onSettingsChanged = [] { PostMessageW(g_wnd, WM_SETTINGS, 0, 0); };
    g_streamer = std::make_unique<Streamer>(g_opt, ev);
    g_streamer->setWatching(ui::window() != nullptr);
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
    g_devices.clear();
    g_stats.clear();
}

// Ayar değişti: bağlantı kopmadan uygulanır (hizmet modunda ajan kayıt defterindeki değişikliği görür)
void applyOptions() {
    if (g_streamer) g_streamer->update(g_opt);
}

// Cihaza bağlan / bağlantısını kes (anahtar boşsa hepsinin); hizmet modunda ajana komut olarak
void connectDevice(const std::string& key) {
    if (g_serviceMode) config::sendCommand("baglan " + key);
    else if (g_streamer) g_streamer->connect(key);
}

void disconnectDevice(const std::string& key) {
    if (g_serviceMode) config::sendCommand(key.empty() ? std::string("kes") : "kes " + key);
    else if (g_streamer) g_streamer->disconnect(key);
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

// Sanal monitörün şimdiki çözünürlüğü (yatay yöndeki boyut) ve yönü
bool currentMode(const std::wstring& gdi, ui::Resolution& res, int& rotation) {
    DEVMODEW cur{};
    cur.dmSize = sizeof cur;
    if (gdi.empty() || !EnumDisplaySettingsExW(gdi.c_str(), ENUM_CURRENT_SETTINGS, &cur, 0)) return false;
    const bool portrait = cur.dmDisplayOrientation & 1;
    rotation = int(cur.dmDisplayOrientation) * 90;
    res = portrait ? ui::Resolution{int(cur.dmPelsHeight), int(cur.dmPelsWidth)}
                   : ui::Resolution{int(cur.dmPelsWidth), int(cur.dmPelsHeight)};
    return true;
}

// Görüntü sayfasındaki cihazın monitör modları: Windows ekran ayarlarındaki listenin aynısı (yatay yöndeki
// boyutlarla). Seçili cihaz ayrıldıysa ilk bağlı cihaz.
void readDisplayModes() {
    g_modes.clear();
    const DeviceInfo* d = deviceByKey(g_displayKey);
    if (!d || d->state != DeviceState::Connected) {
        d = nullptr;
        for (const auto& x : g_devices)
            if (x.state == DeviceState::Connected) {
                d = &x;
                break;
            }
        g_displayKey = d ? d->key : std::string();
    }
    if (!d || d->gdiName.empty()) return;
    DEVMODEW cur{};
    cur.dmSize = sizeof cur;
    if (!EnumDisplaySettingsExW(d->gdiName.c_str(), ENUM_CURRENT_SETTINGS, &cur, 0)) return;
    const bool portrait = cur.dmDisplayOrientation & 1;
    auto land = [portrait](const DEVMODEW& m) {
        return portrait ? ui::Resolution{int(m.dmPelsHeight), int(m.dmPelsWidth)}
                        : ui::Resolution{int(m.dmPelsWidth), int(m.dmPelsHeight)};
    };
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    for (DWORD i = 0; EnumDisplaySettingsExW(d->gdiName.c_str(), i, &dm, 0); i++) {
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
    // Kurulumda cihaza aynı anda iki yayıncı bağlanmasın: önce kendi yayınımızı durdur
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
    if (g_serviceMode && ui::window()) config::set(L"Izle", 1);  // pencere açık: ajan cihazları listelesin
    startStreamer();
    updateTray();
}

// --- Ayar penceresiyle bağlantı ---

class AppHost : public ui::Host {
public:
    ui::Model model() override {
        ui::Model m;
        m.opt = g_opt;
        g_viewKeys.clear();
        for (const auto& d : g_devices) {
            ui::DeviceView v;
            v.name = widen(d.name);
            v.link = widen(d.link);
            v.state = d.state;
            v.mode = d.mode;
            v.width = d.width;
            v.height = d.height;
            if (d.state == DeviceState::Connected) {
                m.connected++;
                auto it = g_stats.find(d.key);
                if (it != g_stats.end()) {
                    v.latency = it->second.latency;
                    v.data = it->second.data;
                    v.fps = it->second.fps;
                }
                currentMode(d.gdiName, v.current, v.rotation);
                if (d.key == g_displayKey) m.displayDevice = int(m.devices.size());
            }
            m.devices.push_back(std::move(v));
            g_viewKeys.push_back(d.key);
        }
        m.modes = g_modes;
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
        auto keyAt = [](int i) { return i >= 0 && i < int(g_viewKeys.size()) ? g_viewKeys[size_t(i)] : std::string(); };
        switch (a) {
            case ui::Act::Reload:
                reloadCaches();
                break;
            case ui::Act::Closed:
                // Ajan ölçüm yazmayı ve (otomatik bağlanma kapalıysa) cihaz aramayı bıraksın
                if (g_serviceMode) config::set(L"Izle", 0);
                else if (g_streamer) g_streamer->setWatching(false);
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
            case ui::Act::Connect:
                if (!keyAt(v).empty()) connectDevice(keyAt(v));
                break;
            case ui::Act::Disconnect:
                if (v < 0) disconnectDevice({});
                else if (!keyAt(v).empty()) disconnectDevice(keyAt(v));
                break;
            case ui::Act::DisplayDevice:
                g_displayKey = keyAt(v);
                readDisplayModes();
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
                if (const DeviceInfo* d = deviceByKey(g_displayKey);
                    d && !d->deviceId.empty() && v >= 0 && v < int(g_modes.size())) {
                    config::saveResolution(d->deviceId, g_modes[size_t(v)].width, g_modes[size_t(v)].height);
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
    if (!ui::window()) {
        // Pencere açıkken cihazlar aranır (otomatik bağlanma kapalıyken de listede görünsünler); hizmet
        // modunda ajan ayrıca ölçüm yazar
        if (g_serviceMode) config::set(L"Izle", 1);
        else if (g_streamer) g_streamer->setWatching(true);
    }
    reloadCaches();
    ui::show(&g_host, connectedCount() ? g_iconOn : g_iconIdle);
}

void showMenu() {
    HMENU m = CreatePopupMenu();
    const int n = connectedCount();
    g_viewKeys.clear();
    for (const auto& d : g_devices) g_viewKeys.push_back(d.key);

    // Bağlı cihazlar (gri satırlar)
    if (n == 0) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, W(i18n::Waiting));
    for (const auto& d : g_devices) {
        if (d.state != DeviceState::Connected) continue;
        std::wstring line = std::wstring(W(i18n::Connected)) + L": " + widen(d.name) + L" (" + widen(d.link) + L")";
        auto it = g_stats.find(d.key);
        if (it != g_stats.end() && !it->second.latency.empty())
            line += L" · " + std::wstring(W(i18n::Latency)) + L" " + it->second.latency;
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, line.c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | MF_DEFAULT, kMenuOpen, W(i18n::MenuOpen));
    // Cihaz başına: bağlıysa kes, değilse bağlan
    bool any = false;
    for (size_t i = 0; i < g_devices.size() && i < 100; i++) {
        const DeviceInfo& d = g_devices[i];
        if (d.state == DeviceState::AppMissing) continue;
        if (!any) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        any = true;
        const bool on = d.state == DeviceState::Connected;
        const std::wstring text = i18n::F(on ? i18n::MenuDisconnectFrom : i18n::MenuConnectTo, widen(d.name).c_str());
        AppendMenuW(m, MF_STRING | (d.state == DeviceState::Connecting ? MF_GRAYED : 0),
                    (on ? kMenuDisconnect : kMenuConnect) + UINT(i), text.c_str());
    }
    if (n > 1) AppendMenuW(m, MF_STRING, kMenuDisconnectAll, W(i18n::DisconnectAll));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
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
    auto keyAt = [](UINT i) { return i < g_viewKeys.size() ? g_viewKeys[i] : std::string(); };
    if (id >= kMenuDisconnect && id < kMenuDisconnect + 100) {
        if (!keyAt(id - kMenuDisconnect).empty()) disconnectDevice(keyAt(id - kMenuDisconnect));
        return;
    }
    if (id >= kMenuConnect && id < kMenuConnect + 100) {
        if (!keyAt(id - kMenuConnect).empty()) connectDevice(keyAt(id - kMenuConnect));
        return;
    }
    switch (id) {
        case kMenuOpen:
            openUi();
            break;
        case kMenuDisconnectAll:
            disconnectDevice({});
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
bool parseStats(const std::string& line, Measure& out) {
    int frames = 0, pushers = 0;
    float mbps = 0, rtt = 0, e2e = 0;
    if (std::sscanf(line.c_str(), "%d kare +%d itici %f Mbit/s RTT %f ms", &frames, &pushers, &mbps, &rtt) != 4) return false;
    const size_t p = line.find("PC→çizim");
    if (p == std::string::npos || std::sscanf(line.c_str() + p + std::strlen("PC→çizim"), "%f", &e2e) != 1) return false;
    wchar_t buf[64];
    if (e2e > 0) {
        swprintf(buf, 64, L"%.0f ms", double(e2e));
        out.latency = buf;
    } else {
        out.latency.clear();  // ekran değişmiyorsa ölçülecek kare yok
    }
    swprintf(buf, 64, L"%.1f Mbit/s", double(mbps));
    out.data = buf;
    swprintf(buf, 64, L"%d fps", frames);
    out.fps = buf;
    return true;
}

// Cihaz listesi geldi: bağlanan / ayrılan cihazlar için bildirim
void onDevices(std::vector<DeviceInfo> list) {
    auto connectedIn = [](const std::vector<DeviceInfo>& l, const std::string& key) {
        return std::any_of(l.begin(), l.end(),
                           [&](const DeviceInfo& d) { return d.key == key && d.state == DeviceState::Connected; });
    };
    bool newlyConnected = false;
    for (const auto& d : list) {
        if (d.state == DeviceState::Connected && !connectedIn(g_devices, d.key)) {
            notify(W(i18n::NotifyConnected), widen(d.name) + L" (" + widen(d.link) + L")");
            newlyConnected = true;
        }
    }
    for (const auto& d : g_devices) {
        if (d.state == DeviceState::Connected && !connectedIn(list, d.key)) {
            notify(W(i18n::NotifyDisconnected), widen(d.name) + L" (" + widen(d.link) + L")");
            g_stats.erase(d.key);
        }
    }
    if (newlyConnected) g_notice = Notice(0);  // bağlandı: uyarının konusu çözüldü
    g_devices = std::move(list);
    readDisplayModes();
    updateTray();
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
        case WM_DEVICES: {
            std::unique_ptr<std::vector<DeviceInfo>> list(reinterpret_cast<std::vector<DeviceInfo>*>(l));
            onDevices(std::move(*list));
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
            std::unique_ptr<std::pair<std::string, std::string>> s(reinterpret_cast<std::pair<std::string, std::string>*>(l));
            const DeviceInfo* d = deviceByKey(s->first);
            Measure m;
            if (d && d->state == DeviceState::Connected && parseStats(s->second, m)) g_stats[s->first] = m;
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
        if (role == L"--windows-ile-baslat") {  // kurulum programı, oturum açmış kullanıcı adına
            setAutostart(true);
            return 0;
        }
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
