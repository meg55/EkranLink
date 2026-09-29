// ekran-surucu.exe — EKRAN sanal ekran sürücüsü için kök aygıt kurma/kaldırma (devcon benzeri, yönetici gerekir).
//
//   ekran-surucu kur <yol\ekranvd.inf>   aygıtı oluştur (yoksa) ve sürücüyü kur/güncelle
//   ekran-surucu kaldir                  aygıtı kaldır (sürücü paketi ayrıca: pnputil /delete-driver)
//   ekran-surucu durum                   aygıt var mı, çalışıyor mu

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

const wchar_t* kHwid = EKRANVD_HARDWARE_ID;

bool hasHwid(HDEVINFO set, SP_DEVINFO_DATA& dev) {
    wchar_t buf[1024] = {};
    DWORD type = 0, size = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, SPDRP_HARDWAREID, &type, reinterpret_cast<BYTE*>(buf),
                                           sizeof buf - 4, &size))
        return false;
    for (const wchar_t* p = buf; *p; p += wcslen(p) + 1)
        if (_wcsicmp(p, kHwid) == 0) return true;
    return false;
}

// Donanım kimliği eşleşen aygıtların listesi (verilen fonksiyon her biri için çağrılır)
template <typename F>
int forEachDevice(F fn) {
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    SP_DEVINFO_DATA dev{sizeof dev};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); i++) {
        if (hasHwid(set, dev)) {
            n++;
            fn(set, dev);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return n;
}

void printError(const char* what) {
    DWORD e = GetLastError();
    wchar_t msg[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, msg, 511, nullptr);
    std::fwprintf(stderr, L"HATA: %hs (0x%08lx) %ls\n", what, e, msg);
}

int install(const wchar_t* infArg) {
    wchar_t inf[MAX_PATH];
    if (!GetFullPathNameW(infArg, MAX_PATH, inf, nullptr)) {
        printError("INF yolu");
        return 1;
    }

    if (forEachDevice([](HDEVINFO, SP_DEVINFO_DATA&) {}) == 0) {
        GUID classGuid;
        wchar_t className[MAX_CLASS_NAME_LEN];
        if (!SetupDiGetINFClassW(inf, &classGuid, className, MAX_CLASS_NAME_LEN, nullptr)) {
            printError("SetupDiGetINFClass");
            return 1;
        }
        HDEVINFO set = SetupDiCreateDeviceInfoList(&classGuid, nullptr);
        SP_DEVINFO_DATA dev{sizeof dev};
        if (set == INVALID_HANDLE_VALUE ||
            !SetupDiCreateDeviceInfoW(set, className, &classGuid, nullptr, nullptr, DICD_GENERATE_ID, &dev)) {
            printError("SetupDiCreateDeviceInfo");
            return 1;
        }
        // Çift sonlandırılmış çoklu dizgi
        std::vector<wchar_t> hwid(kHwid, kHwid + wcslen(kHwid) + 1);
        hwid.push_back(0);
        if (!SetupDiSetDeviceRegistryPropertyW(set, &dev, SPDRP_HARDWAREID, reinterpret_cast<BYTE*>(hwid.data()),
                                               DWORD(hwid.size() * sizeof(wchar_t))) ||
            !SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &dev)) {
            printError("Aygıt kaydı");
            SetupDiDestroyDeviceInfoList(set);
            return 1;
        }
        SetupDiDestroyDeviceInfoList(set);
        std::printf("Kök aygıt oluşturuldu: %ls\n", kHwid);
    } else {
        std::printf("Aygıt zaten var, sürücü güncelleniyor\n");
    }

    BOOL reboot = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, kHwid, inf, INSTALLFLAG_FORCE, &reboot)) {
        printError("UpdateDriverForPlugAndPlayDevices");
        return 1;
    }
    std::printf("Sürücü kuruldu%s\n", reboot ? " (yeniden başlatma gerekiyor)" : "");
    return 0;
}

int uninstall() {
    int removed = 0;
    int n = forEachDevice([&](HDEVINFO set, SP_DEVINFO_DATA& dev) {
        // devcon remove ile aynı: DIF_REMOVE (DiUninstallDevice MinGW kütüphanesinde yok)
        if (SetupDiCallClassInstaller(DIF_REMOVE, set, &dev)) removed++;
        else printError("DIF_REMOVE");
    });
    std::printf("%d aygıttan %d tanesi kaldırıldı\n", n, removed);
    return removed == n ? 0 : 1;
}

int status() {
    int n = forEachDevice([](HDEVINFO set, SP_DEVINFO_DATA& dev) {
        ULONG st = 0, problem = 0;
        CONFIGRET cr = CM_Get_DevNode_Status(&st, &problem, dev.DevInst, 0);
        wchar_t id[MAX_DEVICE_ID_LEN] = {};
        SetupDiGetDeviceInstanceIdW(set, &dev, id, MAX_DEVICE_ID_LEN, nullptr);
        if (cr != CR_SUCCESS) std::printf("%ls: kurulu değil / devre dışı\n", id);
        else if (st & DN_HAS_PROBLEM) std::printf("%ls: SORUN (kod %lu)\n", id, problem);
        else std::printf("%ls: %s\n", id, (st & DN_STARTED) ? "çalışıyor" : "başlatılmadı");
    });
    if (n == 0) std::printf("EKRAN sanal ekran aygıtı yok\n");
    return n > 0 ? 0 : 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc >= 3 && _wcsicmp(argv[1], L"kur") == 0) return install(argv[2]);
    if (argc >= 2 && _wcsicmp(argv[1], L"kaldir") == 0) return uninstall();
    if (argc >= 2 && _wcsicmp(argv[1], L"durum") == 0) return status();
    std::printf("Kullanım: ekran-surucu kur <ekranvd.inf> | kaldir | durum\n");
    return 2;
}
