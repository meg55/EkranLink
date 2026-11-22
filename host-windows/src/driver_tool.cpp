// ekranlink-surucu.exe — EkranLink sanal ekran sürücüsünü kurar / kaldırır (yönetici gerekir).
//
//   ekranlink-surucu kur <yol\ekranlinkvd.inf>   sürücüyü sürücü deposuna koyar; eskilerini temizler
//   ekranlink-surucu kaldir                      sürücüyü ve EkranLink aygıt kayıtlarını kaldırır
//   ekranlink-surucu durum                       sürücü kurulu mu, aygıt şu an var mı
//
// Kurulum aygıt OLUŞTURMAZ: aygıtı EkranLink, bir cihaz İkinci ekran / Tek ekran ile bağlanınca yazılım aygıtı olarak
// oluşturur (virtual_display.cpp), iş bitince Windows kaldırır. Kullanılmazken Aygıt Yöneticisi'nde görünmez.
// 0.5.3 ve öncesi kalıcı bir kök aygıt kuruyordu; "kur" onu bulursa kaldırır.

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <setupapi.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "../../driver/src/ekranvd-ioctl.h"

namespace {

constexpr const wchar_t* kCatalog = L"ekranlinkvd.cat";  // EkranLink sürücü paketlerini tanımanın yolu

void printError(const char* what, DWORD e = GetLastError()) {
    wchar_t msg[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, msg, 511, nullptr);
    std::fwprintf(stderr, L"HATA: %hs (0x%08lx) %ls\n", what, e, msg);
}

bool hasHwid(HDEVINFO set, SP_DEVINFO_DATA& dev, const wchar_t* hwid) {
    wchar_t buf[1024] = {};
    DWORD type = 0, size = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, SPDRP_HARDWAREID, &type, reinterpret_cast<BYTE*>(buf),
                                           sizeof buf - 4, &size))
        return false;
    for (const wchar_t* p = buf; *p; p += wcslen(p) + 1)
        if (_wcsicmp(p, hwid) == 0) return true;
    return false;
}

// Bu donanım kimliğine sahip aygıt kayıtları (şu an olmayanlar dahil); fn her biri için çağrılır
template <typename F>
int forEachDevice(const wchar_t* hwid, F fn) {
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    SP_DEVINFO_DATA dev{sizeof dev};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); i++) {
        if (hasHwid(set, dev, hwid)) {
            n++;
            fn(set, dev);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return n;
}

int removeDevices(const wchar_t* hwid) {
    int removed = 0;
    forEachDevice(hwid, [&](HDEVINFO set, SP_DEVINFO_DATA& dev) {
        // devcon remove ile aynı: DIF_REMOVE (DiUninstallDevice MinGW kütüphanesinde yok)
        if (SetupDiCallClassInstaller(DIF_REMOVE, set, &dev)) removed++;
        else printError("Aygıt kaldırılamadı");
    });
    return removed;
}

// Sürücü deposundaki EkranLink paketleri (C:\Windows\INF\oemNN.inf adlarıyla)
std::vector<std::wstring> ourPackages() {
    std::vector<std::wstring> out;
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    const std::wstring dir = std::wstring(win) + L"\\INF\\";
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"oem*.inf").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        HINF inf = SetupOpenInfFileW((dir + fd.cFileName).c_str(), nullptr, INF_STYLE_WIN4, nullptr);
        if (inf == INVALID_HANDLE_VALUE) continue;
        wchar_t cat[MAX_PATH] = {};
        if (SetupGetLineTextW(nullptr, inf, L"Version", L"CatalogFile", cat, MAX_PATH, nullptr) &&
            _wcsicmp(cat, kCatalog) == 0)
            out.emplace_back(fd.cFileName);
        SetupCloseInfFile(inf);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return out;
}

void setInstalledInf(const wchar_t* name) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, EKRANVD_SETTINGS_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr))
        return;
    if (name) RegSetValueExW(k, L"Inf", 0, REG_SZ, reinterpret_cast<const BYTE*>(name), DWORD((wcslen(name) + 1) * 2));
    else RegDeleteValueW(k, L"Inf");
    RegCloseKey(k);
}

int install(const wchar_t* infArg) {
    wchar_t inf[MAX_PATH];
    if (!GetFullPathNameW(infArg, MAX_PATH, inf, nullptr)) {
        printError("INF yolu");
        return 1;
    }

    // 1) Eski kalıcı kök aygıt (0.5.3 ve öncesi)
    if (const int n = removeDevices(EKRANVD_LEGACY_HARDWARE_ID)) std::printf("Eski kalıcı aygıt kaldırıldı (%d)\n", n);

    // 2) Sürücüyü depoya koy (aygıt oluşturmadan)
    wchar_t dest[MAX_PATH] = {};
    wchar_t* name = nullptr;
    if (!SetupCopyOEMInfW(inf, nullptr, SPOST_PATH, 0, dest, MAX_PATH, nullptr, &name) || !name) {
        printError("Sürücü depoya eklenemedi (imza güvenilir mi?)");
        return 1;
    }
    std::printf("Sürücü depoya eklendi: %ls\n", name);

    // 3) Eski EkranLink paketleri
    for (const std::wstring& old : ourPackages()) {
        if (_wcsicmp(old.c_str(), name) == 0) continue;
        if (SetupUninstallOEMInfW(old.c_str(), SUOI_FORCEDELETE, nullptr)) std::printf("Eski sürücü silindi: %ls\n", old.c_str());
        else printError("Eski sürücü silinemedi");
    }
    setInstalledInf(name);

    // 4) Aygıt şu an varsa (EkranLink bağlıyken güncelleme) yeni sürücüye geçsin
    BOOL reboot = FALSE;
    if (UpdateDriverForPlugAndPlayDevicesW(nullptr, EKRANVD_HARDWARE_ID, inf, INSTALLFLAG_FORCE, &reboot))
        std::printf("Çalışan aygıt yeni sürücüye geçti%s\n", reboot ? " (yeniden başlatma gerekiyor)" : "");
    else if (GetLastError() != ERROR_NO_SUCH_DEVINST)
        printError("Çalışan aygıt güncellenemedi");
    std::printf("Tamam. Aygıt, bir cihaz İkinci ekran ya da Tek ekran ile bağlanınca oluşur.\n");
    return 0;
}

int uninstall() {
    const int devs = removeDevices(EKRANVD_HARDWARE_ID) + removeDevices(EKRANVD_LEGACY_HARDWARE_ID);
    int pkgs = 0, failed = 0;
    for (const std::wstring& p : ourPackages()) {
        if (SetupUninstallOEMInfW(p.c_str(), SUOI_FORCEDELETE, nullptr)) pkgs++;
        else {
            failed++;
            printError("Sürücü silinemedi");
        }
    }
    setInstalledInf(nullptr);
    std::printf("%d aygıt kaydı ve %d sürücü paketi kaldırıldı\n", devs, pkgs);
    return failed ? 1 : 0;
}

int status() {
    const std::vector<std::wstring> pkgs = ourPackages();
    if (pkgs.empty()) std::printf("Sürücü kurulu değil\n");
    for (const std::wstring& p : pkgs) std::printf("Sürücü: %ls\n", p.c_str());
    for (const wchar_t* hwid : {EKRANVD_HARDWARE_ID, EKRANVD_LEGACY_HARDWARE_ID}) {
        forEachDevice(hwid, [&](HDEVINFO set, SP_DEVINFO_DATA& dev) {
            ULONG st = 0, problem = 0;
            const CONFIGRET cr = CM_Get_DevNode_Status(&st, &problem, dev.DevInst, 0);
            wchar_t id[MAX_DEVICE_ID_LEN] = {};
            SetupDiGetDeviceInstanceIdW(set, &dev, id, MAX_DEVICE_ID_LEN, nullptr);
            const bool legacy = hwid != EKRANVD_HARDWARE_ID;
            if (cr != CR_SUCCESS) std::printf("%ls: şu an yok%s\n", id, legacy ? " (eski kök aygıt)" : "");
            else if (st & DN_HAS_PROBLEM) std::printf("%ls: SORUN (kod %lu)\n", id, problem);
            else std::printf("%ls: %s%s\n", id, (st & DN_STARTED) ? "çalışıyor" : "başlatılmadı",
                             legacy ? " (eski kök aygıt; \"kur\" kaldırır)" : "");
        });
    }
    return pkgs.empty() ? 2 : 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc >= 3 && _wcsicmp(argv[1], L"kur") == 0) return install(argv[2]);
    if (argc >= 2 && _wcsicmp(argv[1], L"kaldir") == 0) return uninstall();
    if (argc >= 2 && _wcsicmp(argv[1], L"durum") == 0) return status();
    std::printf("Kullanım: ekranlink-surucu kur <ekranlinkvd.inf> | kaldir | durum\n");
    return 2;
}
