#include "adb.h"
#include "config.h"
#include "log.h"

#include <windows.h>

#include <atomic>
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

// Seri numarası komut satırına tırnaksız girer: boşluk, tırnak vb. başka argüman ekleyebilirdi
bool validSerial(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == ':' || c == '-'))
            return false;
    return true;
}

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
    // Hizmet / ajan SYSTEM olarak çalışır: ortam değişkenlerindeki ya da PATH'teki bir adb.exe onun yetkisiyle
    // çalışırdı. Yalnızca kurulumun Program Files'a koyduğu adb.
    if (config::machineStore()) return {};
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
        std::string serial = line.substr(0, tab);
        if (!validSerial(serial)) {
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true)) {
                for (char& c : serial)
                    if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) c = '?';
                logf("adb: geçersiz seri numaralı cihaz atlandı (%.64s)", serial.c_str());
            }
            continue;
        }
        list.push_back(std::move(serial));
    }
    return list;
}

bool isNetworkSerial(const std::string& serial) {
    // adb connect: "IP:port"; kablosuz hata ayıklama (Android 11+) mDNS adı: "adb-...._adb-tls-connect._tcp"
    return serial.find(':') != std::string::npos || serial.find("_adb-tls-connect") != std::string::npos ||
           serial.find("._tcp") != std::string::npos;
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

bool prepareUsb(const std::wstring& adbPath, const std::string& serial, unsigned& localPort, unsigned remotePort) {
    std::string out;
    wchar_t fwd[64];
    swprintf(fwd, 64, L"forward tcp:%u tcp:%u", localPort, remotePort);
    if (!run(adbPath, to(serial) + fwd, &out)) {
        logf("USB: adb forward başarısız (%s): %s", serial.c_str(), out.c_str());
        return false;
    }
    if (localPort == 0) {
        localPort = unsigned(std::atoi(out.c_str()));  // "tcp:0" ile adb seçtiği portu yazar
        if (localPort == 0) {
            logf("USB: adb port bildirmedi (%s)", out.c_str());
            return false;
        }
    }
    return true;
}

App probe(const std::wstring& adbPath, const std::string& serial, std::string& id, std::string& name) {
    // Uygulama sonuç verisi olarak "kimlik|ad" döndürür; kurulu değilse yayın alıcısız kalır (result=0)
    std::string out;
    if (!run(adbPath, to(serial) + L"shell am broadcast -f 32 -n org.ekranlink.android/.AutoStartReceiver --ez kimlik true",
             &out))
        return App::Unknown;
    const size_t r = out.find("result=");
    if (r == std::string::npos || std::atoi(out.c_str() + r + 7) == 0) return appState(adbPath, serial);
    const size_t d = out.find("data=\"");
    if (d != std::string::npos) {
        const size_t b = d + 6, e = out.find('"', b), bar = out.find('|', b);
        if (e != std::string::npos && bar != std::string::npos && bar < e) {
            id = out.substr(b, bar - b);
            name = out.substr(bar + 1, e - bar - 1);
        }
    }
    if (name.empty()) {
        std::string maker, model;
        run(adbPath, to(serial) + L"shell getprop ro.product.manufacturer", &maker, 3000);
        run(adbPath, to(serial) + L"shell getprop ro.product.model", &model, 3000);
        auto trim = [](std::string s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
            return s;
        };
        name = trim(maker) + (maker.empty() ? "" : " ") + trim(model);
        if (name == " " || name.empty()) name = serial;
    }
    return App::Installed;
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
