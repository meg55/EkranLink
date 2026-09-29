#include "desktop.h"

#include <windows.h>

#include <atomic>

#include "log.h"

namespace ekran {

namespace {

std::atomic<bool> g_follow{false};

std::wstring nameOf(HDESK d) {
    wchar_t name[64] = {};
    DWORD need = 0;
    if (!GetUserObjectInformationW(d, UOI_NAME, name, sizeof name, &need)) return {};
    return name;
}

}  // namespace

std::wstring inputDesktopName() {
    HDESK d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!d) return {};
    std::wstring n = nameOf(d);
    CloseDesktop(d);
    return n;
}

void enableDesktopFollowing(bool on) { g_follow = on; }

bool followInputDesktop() {
    if (!g_follow) return false;
    // İş parçacığı başına: bağlı olduğu masaüstü (ilk baştaki süreç masaüstü bizim değil, kapatılmaz)
    thread_local wchar_t current[64];  // düz dizi: yıkıcısı olan thread_local çalışma zamanını büyütür
    thread_local HDESK held = nullptr;
    HDESK d = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!d) return false;
    const std::wstring n = nameOf(d);
    if (n.empty() || n == current || n.size() >= 64 || !SetThreadDesktop(d)) {
        CloseDesktop(d);
        return false;
    }
    if (held) CloseDesktop(held);  // artık bu iş parçacığı ona bağlı değil
    held = d;
    if (current[0]) logf("Masaüstü: %ls", n.c_str());
    wcscpy(current, n.c_str());
    return true;
}

}  // namespace ekran
