#include "adb.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace ekran::adb {

namespace {

bool exists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring env(const wchar_t* name) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring();
}

std::wstring exeDir() {
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p, n);
    return s.substr(0, s.find_last_of(L'\\'));
}

// "-s SERİ " (komut o cihaza gitsin; birden çok cihaz takılıyken adb aksi halde hata verir)
std::wstring to(const std::string& serial) { return L"-s " + std::wstring(serial.begin(), serial.end()) + L" "; }

}  // namespace

App appState(const std::wstring& adbPath, const std::string& serial) {
    // adb başarısızsa (ör. tablette "USB hata ayıklamaya izin ver" bekleniyor) bilinmiyor
    std::string out;
    if (!run(adbPath, to(serial) + L"shell pm list packages org.ekranlink.android", &out)) return App::Unknown;
    return out.find("package:org.ekranlink.android") != std::string::npos ? App::Installed : App::Missing;
}

std::wstring find() {
    // Paketle gelen adb (exe'nin yanında) önce
    for (const wchar_t* rel : {L"\\adb\\adb.exe", L"\\adb.exe"}) {
        std::wstring p = exeDir() + rel;
        if (exists(p)) return p;
    }
    for (const wchar_t* var : {L"ANDROID_HOME", L"ANDROID_SDK_ROOT"}) {
        std::wstring root = env(var);
        if (!root.empty() && exists(root + L"\\platform-tools\\adb.exe")) return root + L"\\platform-tools\\adb.exe";
    }
    std::wstring local = env(L"LOCALAPPDATA") + L"\\Android\\Sdk\\platform-tools\\adb.exe";
    if (exists(local)) return local;
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"adb.exe", nullptr, MAX_PATH, buf, nullptr) > 0) return buf;
    return {};
}

bool run(const std::wstring& adbPath, const std::wstring& args, std::string* output, int timeoutMs) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + adbPath + L"\" " + args;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return false;
    }

    std::string out;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0) out.append(buf, n);
    CloseHandle(rd);

    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, DWORD(timeoutMs)) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (output) *output = std::move(out);
    return code == 0;
}

std::vector<std::string> devices(const std::wstring& adbPath) {
    // "adb devices": "SERİ<TAB>device" satırları (yetkisiz / çevrimdışı cihazlar atlanır)
    std::string out;
    std::vector<std::string> list;
    if (!run(adbPath, L"devices", &out, 3000)) return list;
    size_t p = 0;
    while (p < out.size()) {
        size_t e = out.find('\n', p);
        if (e == std::string::npos) e = out.size();
        std::string line = out.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || line.compare(tab + 1, std::string::npos, "device") != 0) continue;
        list.push_back(line.substr(0, tab));
    }
    return list;
}

void screenOff(const std::wstring& adbPath, const std::string& serial, int sdk) {
    if (sdk >= 20) {
        run(adbPath, to(serial) + L"shell input keyevent 223");  // KEYCODE_SLEEP: yalnızca kapatır
        return;
    }
    // 4.x: yalnızca güç tuşu var ve ekranı aç/kapa yapar; önce açık olduğundan emin ol
    std::string out;
    if (run(adbPath, to(serial) + L"shell dumpsys power", &out) &&
        (out.find("mScreenOn=true") != std::string::npos || out.find("Display Power: state=ON") != std::string::npos))
        run(adbPath, to(serial) + L"shell input keyevent 26");
}

bool prepareUsb(const std::wstring& adbPath, const std::string& serial, unsigned port) {
    std::string out;
    wchar_t fwd[64];
    swprintf(fwd, 64, L"forward tcp:%u tcp:%u", port, port);
    if (!run(adbPath, to(serial) + fwd, &out)) {
        logf("USB: adb forward başarısız: %s", out.c_str());
        return false;
    }
    return true;
}

Launch requestLaunch(const std::wstring& adbPath, const std::string& serial) {
    // Açık bileşene yayın; -f 32 (FLAG_INCLUDE_STOPPED_PACKAGES): hiç açılmamış uygulamaya da ulaşsın.
    // Sonuç kodu tabletin kararı (AutoStartReceiver): 1 izin var, 2 otomatik açma kapalı, 3 zaten açık.
    // Uygulamayı tablet değil biz başlatırız: Android 10+ arka plandan ekran açılmasına izin vermez, adb verir.
    std::string out;
    if (!run(adbPath, to(serial) + L"shell am broadcast -f 32 -n org.ekranlink.android/.AutoStartReceiver", &out))
        return Launch::Failed;
    const size_t p = out.find("result=");
    const int r = p == std::string::npos ? -1 : std::atoi(out.c_str() + p + 7);
    if (r == 2) return Launch::Declined;
    if (r == 3) return Launch::AlreadyOpen;
    // 1 ya da eski sürüm (alıcı yok): ekranı uyandır (KEYCODE_WAKEUP 4.4 öncesinde yok) ve başlat
    if (run(adbPath, to(serial) + L"shell dumpsys power", &out) &&
        (out.find("mScreenOn=false") != std::string::npos || out.find("Display Power: state=OFF") != std::string::npos))
        run(adbPath, to(serial) + L"shell input keyevent 26");
    run(adbPath, to(serial) + L"shell am start -n org.ekranlink.android/.MainActivity");
    return Launch::Started;
}

}  // namespace ekran::adb
