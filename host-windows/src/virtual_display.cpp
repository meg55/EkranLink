#include "virtual_display.h"

#include <setupapi.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

#include "../../driver/src/ekranvd-ioctl.h"
#include "crypto.h"
#include "log.h"

namespace ekran {

namespace {

HANDLE openDevice();

// ---- Yazılım aygıtı: sürücü yalnızca kullanılırken bir aygıta bağlıdır ----
// SwDeviceCreate (Windows 8+, cfgmgr32.dll). MinGW'de swdevice.h olmadığı için tanımlar burada.
struct SwCreateInfo {
    ULONG cbSize;
    PCWSTR pszInstanceId;
    PCWSTR pszzHardwareIds;
    PCWSTR pszzCompatibleIds;
    const GUID* pContainerId;
    ULONG CapabilityFlags;
    PCWSTR pszDeviceDescription;
    PCWSTR pszDeviceLocation;
    const SECURITY_DESCRIPTOR* pSecurityDescriptor;
};
using HSwDevice = HANDLE;
using SwCallback = void(WINAPI*)(HSwDevice, HRESULT, PVOID, PCWSTR);
using SwCreateFn = HRESULT(WINAPI*)(PCWSTR, PCWSTR, const SwCreateInfo*, ULONG, const void*, SwCallback, PVOID,
                                    HSwDevice*);
using SwCloseFn = void(WINAPI*)(HSwDevice);
constexpr ULONG kSwSilentInstall = 0x2, kSwDriverRequired = 0x8;
// Son monitör gidince aygıt bu kadar bekler: ayar değişikliği ya da yeniden bağlanma aygıtı yeniden kurdurmasın
constexpr uint64_t kLingerMs = 15000;

struct SwCreateResult {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT hr = E_FAIL;
};

void WINAPI onSwCreated(HSwDevice, HRESULT hr, PVOID ctx, PCWSTR) {
    auto* r = static_cast<SwCreateResult*>(ctx);
    r->hr = hr;
    SetEvent(r->done);
}

// Aygıtı kullanan monitör sayısını tutar: ilk gelen oluşturur, son giden (bekleme süresinden sonra) kaldırır. Aygıtın
// ömrü tanıtıcıya bağlıdır: program kapanır ya da çökerse Windows aygıtı kendisi kaldırır.
class SoftwareDevice {
public:
    static SoftwareDevice& get() {
        static auto* d = new SoftwareDevice;  // bilerek silinmez: arkadaki kapatıcı iş parçacığı kullanabilir
        return *d;
    }

    bool acquire() {
        std::unique_lock<std::mutex> lock(m_);
        closeAt_ = 0;
        cv_.notify_all();
        if (!h_ && !create()) return false;
        users_++;
        return true;
    }

    void release() {
        std::lock_guard<std::mutex> lock(m_);
        if (users_ > 0 && --users_ == 0 && h_) {
            closeAt_ = GetTickCount64() + kLingerMs;
            if (!closerRunning_) {
                closerRunning_ = true;
                std::thread([this] { closer(); }).detach();
            }
            cv_.notify_all();
        }
    }

    // Program kapanırken: kullanan yoksa beklemeden kaldır
    void closeIdle() {
        std::lock_guard<std::mutex> lock(m_);
        if (users_ == 0) closeLocked();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    HSwDevice h_ = nullptr;
    int users_ = 0;
    uint64_t closeAt_ = 0;  // 0: kapatma beklenmiyor
    bool closerRunning_ = false;
    SwCreateFn create_ = nullptr;
    SwCloseFn close_ = nullptr;

    bool create() {
        if (!create_) {
            HMODULE cfg = LoadLibraryExW(L"cfgmgr32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            create_ = cfg ? reinterpret_cast<SwCreateFn>(reinterpret_cast<void*>(GetProcAddress(cfg, "SwDeviceCreate"))) : nullptr;
            close_ = cfg ? reinterpret_cast<SwCloseFn>(reinterpret_cast<void*>(GetProcAddress(cfg, "SwDeviceClose"))) : nullptr;
            if (!create_ || !close_) {
                create_ = nullptr;
                return false;
            }
        }
        static const wchar_t kIds[] = EKRANVD_HARDWARE_ID L"\0";
        SwCreateInfo info{};
        info.cbSize = sizeof info;
        info.pszInstanceId = EKRANVD_INSTANCE_ID;
        info.pszzHardwareIds = kIds;
        info.pszzCompatibleIds = kIds;
        info.CapabilityFlags = kSwSilentInstall | kSwDriverRequired;
        info.pszDeviceDescription = L"EkranLink Sanal Ekran";
        // Geri çağrı zaman aşımından sonra da gelebilir: sonuç yığında değil, o durumda bilerek sızdırılır
        auto* result = new SwCreateResult;
        const uint64_t start = GetTickCount64();
        HSwDevice h = nullptr;
        HRESULT hr = create_(EKRANVD_ENUMERATOR, L"HTREE\\ROOT\\0", &info, 0, nullptr, onSwCreated, result, &h);
        if (SUCCEEDED(hr)) {
            if (WaitForSingleObject(result->done, 15000) == WAIT_OBJECT_0) {
                hr = result->hr;
                CloseHandle(result->done);
                delete result;
            } else {
                hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
        }
        if (FAILED(hr)) {
            logf("Sanal ekran aygıtı oluşturulamadı (0x%08lx)", (unsigned long)hr);
            if (h) close_(h);
            return false;
        }
        // Sürücü başlayıp arayüzünü açınca kullanılabilir
        while (GetTickCount64() - start < 15000) {
            HANDLE dev = openDevice();
            if (dev != INVALID_HANDLE_VALUE) {
                CloseHandle(dev);
                h_ = h;
                logf("Sanal ekran aygıtı oluşturuldu (%llu ms)", (unsigned long long)(GetTickCount64() - start));
                return true;
            }
            Sleep(100);
        }
        logf("Sanal ekran aygıtı oluştu ama sürücü başlamadı; kaldırılıyor");
        close_(h);
        return false;
    }

    void closeLocked() {
        if (!h_) return;
        close_(h_);
        h_ = nullptr;
        closeAt_ = 0;
        logf("Sanal ekran aygıtı kaldırıldı");
    }

    void closer() {
        std::unique_lock<std::mutex> lock(m_);
        while (closeAt_ && h_) {
            const uint64_t now = GetTickCount64();
            if (now >= closeAt_) {
                closeLocked();
                break;
            }
            cv_.wait_for(lock, std::chrono::milliseconds(closeAt_ - now));
        }
        closerRunning_ = false;
    }
};

HANDLE openDevice() {
    HDEVINFO set = SetupDiGetClassDevsW(&EKRANVD::EKRANVD_INTERFACE_GUID, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    HANDLE h = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA ifd{sizeof ifd};
    if (SetupDiEnumDeviceInterfaces(set, nullptr, &EKRANVD::EKRANVD_INTERFACE_GUID, 0, &ifd)) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        std::vector<uint8_t> buf(need);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, need, nullptr, nullptr))
            h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
    }
    SetupDiDestroyDeviceInfoList(set);
    return h;
}

// IddCx hedefinin (bağdaştırıcı LUID + hedef no) Windows'taki etkin kaynağını (\\.\DISPLAYn) bul
bool findGdiName(const LUID& adapter, UINT targetId, std::wstring& name) {
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS)
        return false;
    for (UINT32 i = 0; i < np; i++) {
        const auto& t = paths[i].targetInfo;
        if (t.id != targetId || t.adapterId.LowPart != adapter.LowPart || t.adapterId.HighPart != adapter.HighPart)
            continue;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof src;
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) == ERROR_SUCCESS) {
            name = src.viewGdiDeviceName;
            return true;
        }
    }
    return false;
}

void copyField(char (&dst)[14], const std::string& s) {
    std::memset(dst, 0, sizeof dst);
    std::memcpy(dst, s.data(), s.size() < 13 ? s.size() : 13);
}

}  // namespace

bool VirtualDisplay::driverInstalled() {
    // Kurulum aracı sürücü deposundaki adını yazar (oemNN.inf); paket sonradan silindiyse dosya da yoktur
    wchar_t inf[64];
    DWORD size = sizeof inf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, EKRANVD_SETTINGS_KEY, L"Inf", RRF_RT_REG_SZ, nullptr, inf, &size) != 0)
        return false;
    if (wcspbrk(inf, L"\\/:")) return false;
    wchar_t win[MAX_PATH];
    if (!GetWindowsDirectoryW(win, MAX_PATH)) return false;
    return GetFileAttributesW((std::wstring(win) + L"\\INF\\" + inf).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool VirtualDisplay::canCreateDevice() {
    static const bool elevated = [] {
        HANDLE tok = nullptr;
        TOKEN_ELEVATION e{};
        DWORD n = 0;
        const bool ok = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok) &&
                        GetTokenInformation(tok, TokenElevation, &e, sizeof e, &n) && e.TokenIsElevated;
        if (tok) CloseHandle(tok);
        return ok;
    }();
    return elevated;
}

void VirtualDisplay::closeIdleDevice() { SoftwareDevice::get().closeIdle(); }

GUID VirtualDisplay::guidFor(const std::string& deviceId) {
    const std::string seed = "EKRAN monitor " + deviceId;
    const auto hsh = crypto::sha256(reinterpret_cast<const uint8_t*>(seed.data()), seed.size());
    GUID g;
    std::memcpy(&g, hsh.data(), sizeof g);
    g.Data3 = uint16_t((g.Data3 & 0x0FFF) | 0x5000);  // ad tabanlı GUID (sürüm 5 biçimi)
    g.Data4[0] = uint8_t((g.Data4[0] & 0x3F) | 0x80);
    return g;
}

bool VirtualDisplay::add(const VirtualDisplaySpec& spec, std::wstring& gdiName, int timeoutMs) {
    remove();
    if (!SoftwareDevice::get().acquire()) return false;
    holdsDevice_ = true;
    dev_ = openDevice();
    if (dev_ == INVALID_HANDLE_VALUE) {
        logf("Sanal ekran: sürücü bulunamadı");
        remove();
        return false;
    }

    EKRANVD::VIRTUAL_DISPLAY_ADD_PARAMS p{};
    p.Width = UINT(spec.width);
    p.Height = UINT(spec.height);
    p.RefreshRate = UINT(spec.refreshHz);
    p.MonitorGuid = spec.monitorGuid;
    copyField(p.DeviceName, spec.name);
    copyField(p.SerialNumber, spec.serial);
    p.WidthMm = UINT16(spec.widthMm);
    p.HeightMm = UINT16(spec.heightMm);
    EKRANVD::VIRTUAL_DISPLAY_ADD_OUT out{};
    DWORD got = 0;
    if (!DeviceIoControl(dev_, IOCTL_ADD_VIRTUAL_DISPLAY, &p, sizeof p, &out, sizeof out, &got, nullptr)) {
        logf("Sanal ekran: monitör eklenemedi (hata %lu)", GetLastError());
        CloseHandle(dev_);
        dev_ = INVALID_HANDLE_VALUE;
        return false;
    }
    guid_ = spec.monitorGuid;
    adapter_ = out.AdapterLuid;
    targetId_ = out.TargetId;
    added_ = true;

    // Sürücünün bekçi köpeği 3 sn ses duymazsa monitörü kaldırır (program çökerse ekran kalmasın).
    pinging_ = true;
    pinger_ = std::thread([this] {
        while (pinging_) {
            DWORD n = 0;
            DeviceIoControl(dev_, IOCTL_DRIVER_PING, nullptr, 0, nullptr, 0, &n, nullptr);
            for (int i = 0; i < 10 && pinging_; i++) Sleep(100);
        }
    });

    // Windows yeni monitörü etkinleştirene kadar bekle; kendiliğinden genişletmezse genişlet.
    const DWORD start = GetTickCount();
    bool extended = false;
    while (int(GetTickCount() - start) < timeoutMs) {
        if (findGdiName(out.AdapterLuid, out.TargetId, gdiName)) {
            logf("Sanal ekran: %dx%d @%d Hz, %ls", spec.width, spec.height, spec.refreshHz, gdiName.c_str());
            gdiName_ = gdiName;
            return true;
        }
        if (!extended && GetTickCount() - start > 1500) {
            SetDisplayConfig(0, nullptr, 0, nullptr, SDC_TOPOLOGY_EXTEND | SDC_APPLY);
            extended = true;
        }
        Sleep(100);
    }
    logf("Sanal ekran: monitör eklendi ama Windows etkinleştirmedi; kaldırılıyor");
    remove();  // sonradan etkinleşip görünmeyen bir ekrana pencere taşınmasın
    return false;
}

void VirtualDisplay::remove() {
    restoreLayout();  // monitör hâlâ varken: kayıtlı düzen onu da içeriyor
    pinging_ = false;
    if (pinger_.joinable()) pinger_.join();
    if (dev_ != INVALID_HANDLE_VALUE) {
        if (added_) {
            EKRANVD::VIRTUAL_DISPLAY_REMOVE_PARAMS r{};
            r.MonitorGuid = guid_;
            DWORD n = 0;
            DeviceIoControl(dev_, IOCTL_REMOVE_VIRTUAL_DISPLAY, &r, sizeof r, nullptr, 0, &n, nullptr);
            logf("Sanal ekran kaldırıldı");
        }
        CloseHandle(dev_);
        dev_ = INVALID_HANDLE_VALUE;
    }
    added_ = false;
    gdiName_.clear();
    if (holdsDevice_) {
        holdsDevice_ = false;
        SoftwareDevice::get().release();
    }
}

namespace {

bool queryActive(std::vector<DISPLAYCONFIG_PATH_INFO>& paths, std::vector<DISPLAYCONFIG_MODE_INFO>& modes) {
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return false;
    paths.resize(np);
    modes.resize(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS)
        return false;
    paths.resize(np);
    modes.resize(nm);
    return true;
}

// Tek ekran düzeni süreç genelinde ortak (birden çok cihaz): ilk geçen kaydeder, son çıkan geri yükler
std::mutex g_isoMutex;
int g_isoCount = 0;
std::vector<DISPLAYCONFIG_PATH_INFO> g_savedPaths;
std::vector<DISPLAYCONFIG_MODE_INFO> g_savedModes;

bool sameAdapter(const LUID& a, const LUID& b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }

}  // namespace

bool VirtualDisplay::isolate() {
    if (!added_ || isolated_) return isolated_;
    std::lock_guard<std::mutex> lock(g_isoMutex);
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (!queryActive(paths, modes)) return false;

    int self = -1;
    for (size_t i = 0; i < paths.size(); i++) {
        const auto& t = paths[i].targetInfo;
        if (t.id == targetId_ && sameAdapter(t.adapterId, adapter_)) self = int(i);
    }
    if (self < 0) return false;
    if (paths.size() == 1) return true;  // zaten tek ekran (ör. ekransız mini PC)

    // Geri yükleme için şimdiki düzen (ilk tek ekrana geçen cihaz kaydeder)
    if (g_isoCount == 0) {
        g_savedPaths = paths;
        g_savedModes = modes;
    }

    // Yalnızca EkranLink monitörleri (aynı sürücü bağdaştırıcısı) etkin kalsın: bu cihaz (0, 0)'da ana ekran,
    // diğerleri sağına dizilir
    LONG x = 0;
    auto place = [&](size_t i) {
        const UINT32 src = paths[i].sourceInfo.modeInfoIdx;
        if (src == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || src >= modes.size() ||
            modes[src].infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
            return;
        modes[src].sourceMode.position = {x, 0};
        x += LONG(modes[src].sourceMode.width);
    };
    place(size_t(self));
    int others = 0;
    for (size_t i = 0; i < paths.size(); i++) {
        if (int(i) == self) continue;
        if (sameAdapter(paths[i].targetInfo.adapterId, adapter_)) {
            place(i);
            continue;
        }
        paths[i].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
        paths[i].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        paths[i].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        others++;
    }

    const LONG r = SetDisplayConfig(UINT32(paths.size()), paths.data(), UINT32(modes.size()), modes.data(),
                                    SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);
    if (r != ERROR_SUCCESS) {
        logf("Tek ekran: diğer monitörler kapatılamadı (%ld)", r);
        if (g_isoCount == 0) {
            g_savedPaths.clear();
            g_savedModes.clear();
        }
        return false;
    }
    isolated_ = true;
    g_isoCount++;
    logf("Tek ekran: diğer %d monitör kapatıldı, cihaz ana ekran", others);
    return true;
}

void VirtualDisplay::restoreLayout() {
    if (!isolated_) return;
    isolated_ = false;
    std::lock_guard<std::mutex> lock(g_isoMutex);
    if (--g_isoCount > 0) return;  // başka cihaz hâlâ tek ekranda: düzeni o geri yükleyecek

    // Tek ekrandayken EkranLink monitörlerinin çözünürlüğü / yönü değişmiş olabilir, ya da yeni cihaz bağlanmış
    // olabilir: kayıtlı düzende onları güncelle / ekle, yoksa eski düzen yüklenince eski modlarına döner (ya da
    // kayıtta olmayan monitör kapanır).
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (queryActive(paths, modes)) {
        LONG right = 0;
        for (const auto& sp : g_savedPaths) {
            const UINT32 ss = sp.sourceInfo.modeInfoIdx;
            if ((sp.flags & DISPLAYCONFIG_PATH_ACTIVE) && ss < g_savedModes.size())
                right = std::max(right, g_savedModes[ss].sourceMode.position.x + LONG(g_savedModes[ss].sourceMode.width));
        }
        for (const auto& now : paths) {
            if (!sameAdapter(now.targetInfo.adapterId, adapter_)) continue;
            bool found = false;
            for (auto& saved : g_savedPaths) {
                if (saved.targetInfo.id != now.targetInfo.id || !sameAdapter(saved.targetInfo.adapterId, adapter_))
                    continue;
                found = true;
                saved.targetInfo.rotation = now.targetInfo.rotation;
                const UINT32 ns = now.sourceInfo.modeInfoIdx, ss = saved.sourceInfo.modeInfoIdx;
                if (ns < modes.size() && ss < g_savedModes.size()) {
                    const POINTL pos = g_savedModes[ss].sourceMode.position;
                    g_savedModes[ss].sourceMode = modes[ns].sourceMode;
                    g_savedModes[ss].sourceMode.position = pos;
                }
                const UINT32 nt = now.targetInfo.modeInfoIdx, st = saved.targetInfo.modeInfoIdx;
                if (nt < modes.size() && st < g_savedModes.size()) g_savedModes[st].targetMode = modes[nt].targetMode;
            }
            if (found) continue;
            // Tek ekrandayken bağlanan cihaz: kayıtlı düzenin sağına eklenir
            DISPLAYCONFIG_PATH_INFO add = now;
            const UINT32 ns = now.sourceInfo.modeInfoIdx, nt = now.targetInfo.modeInfoIdx;
            if (ns < modes.size()) {
                DISPLAYCONFIG_MODE_INFO m = modes[ns];
                m.sourceMode.position = {right, 0};
                right += LONG(m.sourceMode.width);
                add.sourceInfo.modeInfoIdx = UINT32(g_savedModes.size());
                g_savedModes.push_back(m);
            }
            if (nt < modes.size()) {
                add.targetInfo.modeInfoIdx = UINT32(g_savedModes.size());
                g_savedModes.push_back(modes[nt]);
            }
            g_savedPaths.push_back(add);
        }
    }
    LONG r = SetDisplayConfig(UINT32(g_savedPaths.size()), g_savedPaths.data(), UINT32(g_savedModes.size()),
                              g_savedModes.data(), SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);
    if (r != ERROR_SUCCESS) {
        // Kayıtlı düzen uymadı (ör. monitör söküldü): Windows'un bu monitör kümesi için bildiği düzen
        logf("Tek ekran: eski düzen yüklenemedi (%ld), genişletilmiş düzene dönülüyor", r);
        r = SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | SDC_TOPOLOGY_EXTEND);
    }
    if (r == ERROR_SUCCESS) logf("Tek ekran: monitörler geri açıldı");
    g_savedPaths.clear();
    g_savedModes.clear();
}

// Yön / çözünürlük Windows'un kaydına da yazılır: monitör bir sonraki bağlanışta doğrudan bu modla açılır
// (yoksa Windows eski kaydıyla açar, EkranLink hemen düzeltir ve ekran fazladan bir kez gidip gelir).
// Tek ekrandayken yazılmaz: o düzen geçicidir, Windows'un kaydına girmemeli.
DWORD VirtualDisplay::persistFlags() const { return isolated_ ? 0 : CDS_UPDATEREGISTRY; }

bool VirtualDisplay::setRotation(int degrees) {
    if (!added_ || gdiName_.empty()) return false;
    const DWORD want = DWORD((degrees / 90) & 3);  // DMDO_DEFAULT, DMDO_90, DMDO_180, DMDO_270
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsExW(gdiName_.c_str(), ENUM_CURRENT_SETTINGS, &dm, 0)) return false;
    if (dm.dmDisplayOrientation == want) return true;
    if ((dm.dmDisplayOrientation ^ want) & 1) {  // yatay ↔ dikey: masaüstü boyutu yer değiştirir
        const DWORD t = dm.dmPelsWidth;
        dm.dmPelsWidth = dm.dmPelsHeight;
        dm.dmPelsHeight = t;
    }
    dm.dmDisplayOrientation = want;
    dm.dmFields = DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT;
    const LONG r = ChangeDisplaySettingsExW(gdiName_.c_str(), &dm, nullptr, persistFlags(), nullptr);
    if (r != DISP_CHANGE_SUCCESSFUL) {
        logf("Sanal ekran: döndürülemedi (%ld)", r);
        return false;
    }
    logf("Sanal ekran: %s", want & 1 ? "dikey" : "yatay");
    return true;
}

bool VirtualDisplay::resolution(int& width, int& height) const {
    if (!added_ || gdiName_.empty()) return false;
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsExW(gdiName_.c_str(), ENUM_CURRENT_SETTINGS, &dm, 0)) return false;
    const bool portrait = dm.dmDisplayOrientation & 1;
    width = int(portrait ? dm.dmPelsHeight : dm.dmPelsWidth);
    height = int(portrait ? dm.dmPelsWidth : dm.dmPelsHeight);
    return true;
}

bool VirtualDisplay::setResolution(int width, int height) {
    if (!added_ || gdiName_.empty()) return false;
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsExW(gdiName_.c_str(), ENUM_CURRENT_SETTINGS, &dm, 0)) return false;
    const bool portrait = dm.dmDisplayOrientation & 1;
    const DWORD w = DWORD(portrait ? height : width), h = DWORD(portrait ? width : height);
    if (dm.dmPelsWidth == w && dm.dmPelsHeight == h) return true;
    dm.dmPelsWidth = w;
    dm.dmPelsHeight = h;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    const LONG r = ChangeDisplaySettingsExW(gdiName_.c_str(), &dm, nullptr, persistFlags(), nullptr);
    if (r != DISP_CHANGE_SUCCESSFUL) {
        logf("Sanal ekran: çözünürlük %dx%d yapılamadı (%ld)", width, height, r);
        return false;
    }
    logf("Sanal ekran: çözünürlük %dx%d", width, height);
    return true;
}

}  // namespace ekran
