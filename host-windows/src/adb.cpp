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

// ekran-android.version: paketleme betiğinin yazdığı versionCode
int bundledVersion() {
    FILE* f = _wfopen((exeDir() + L"\\ekran-android.version").c_str(), L"r");
    if (!f) return 0;
    int v = 0;
    if (std::fscanf(f, "%d", &v) != 1) v = 0;
    std::fclose(f);
    return v;
}

// Tabletteki EKRAN sürümü; kurulu değilse -1
int installedVersion(const std::wstring& adbPath) {
    std::string out;
    if (!run(adbPath, L"shell pm list packages org.ekran.android", &out) ||
        out.find("package:org.ekran.android") == std::string::npos)
        return -1;
    if (!run(adbPath, L"shell dumpsys package org.ekran.android", &out)) return 0;
    size_t p = out.find("versionCode=");
    return p == std::string::npos ? -1 : std::atoi(out.c_str() + p + 12);
}

}  // namespace

// Paketle gelen APK (dağıtım) ya da geliştirme derlemesi
std::wstring bundledApk() {
    const std::wstring dir = exeDir();
    for (const wchar_t* rel : {L"\\ekran-android.apk", L"\\..\\..\\android\\app\\build\\outputs\\apk\\debug\\app-debug.apk"}) {
        std::wstring p = dir + rel;
        if (exists(p)) return p;
    }
    return {};
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

bool hasDevice(const std::wstring& adbPath) {
    std::string out;
    return run(adbPath, L"get-state", &out, 3000) && out.find("device") != std::string::npos;
}

void screenOff(const std::wstring& adbPath, int sdk) {
    if (sdk >= 20) {
        run(adbPath, L"shell input keyevent 223");  // KEYCODE_SLEEP: yalnızca kapatır
        return;
    }
    // 4.x: yalnızca güç tuşu var ve ekranı aç/kapa yapar; önce açık olduğundan emin ol
    std::string out;
    if (run(adbPath, L"shell dumpsys power", &out) &&
        (out.find("mScreenOn=true") != std::string::npos || out.find("Display Power: state=ON") != std::string::npos))
        run(adbPath, L"shell input keyevent 26");
}

bool prepareUsb(const std::wstring& adbPath, unsigned port) {
    std::string out;
    if (!run(adbPath, L"get-state", &out) || out.find("device") == std::string::npos) {
        logf("USB: adb cihaz görmüyor (USB hata ayıklama açık mı?)");
        return false;
    }
    wchar_t fwd[64];
    swprintf(fwd, 64, L"forward tcp:%u tcp:%u", port, port);
    if (!run(adbPath, fwd, &out)) {
        logf("USB: adb forward başarısız: %s", out.c_str());
        return false;
    }
    // Uygulama yoksa veya paketteki sürümden eskiyse kur (USB hata ayıklama açık olduğu için onay gerekmez).
    const std::wstring apk = bundledApk();
    if (!apk.empty()) {
        const int have = installedVersion(adbPath), want = bundledVersion();
        if (have < 0 || (want > 0 && have < want)) {
            logf("USB: tablete EKRAN %s…", have < 0 ? "kuruluyor" : "güncelleniyor");
            if (run(adbPath, L"install -r \"" + apk + L"\"", &out, 120000) && out.find("Success") != std::string::npos) {
                logf("USB: kurulum tamam");
            } else {
                // Farklı imzalı eski sürüm varsa (INSTALL_FAILED_UPDATE_INCOMPATIBLE) kullanıcı kaldırmalı.
                logf("USB: kurulum başarısız: %s", out.c_str());
            }
        }
    }
    // Ekran kapalıysa uyandır (KEYCODE_WAKEUP 4.4 öncesinde yok; güç tuşu).
    if (run(adbPath, L"shell dumpsys power", &out) &&
        (out.find("mScreenOn=false") != std::string::npos || out.find("Display Power: state=OFF") != std::string::npos))
        run(adbPath, L"shell input keyevent 26");
    run(adbPath, L"shell am start -n org.ekran.android/.MainActivity");
    return true;
}

}  // namespace ekran::adb
