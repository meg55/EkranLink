#include "service.h"

#include <winsock2.h>
#include <windows.h>
#include <mfapi.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>

#include <objbase.h>

#include <string>
#include <thread>
#include <vector>

#include "adb.h"
#include "config.h"
#include "i18n.h"
#include "desktop.h"
#include "log.h"
#include "pair_prompt.h"
#include "pairing.h"
#include "streamer.h"

namespace ekran::service {

namespace {

constexpr const wchar_t* kAgentStopEvent = L"Global\\EKRAN-ajan-dur";
constexpr const wchar_t* kAgentMutex = L"Global\\EKRAN-ajan";

std::wstring selfPath() {
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    return std::wstring(p, n);
}

std::wstring dirOf(const std::wstring& path) { return path.substr(0, path.find_last_of(L'\\')); }

std::wstring installDir() {
    wchar_t p[MAX_PATH] = L"C:\\Program Files";
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, p);  // FOLDERID_*: bkz. config.cpp
    return std::wstring(p) + L"\\EKRAN";
}

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ============================== Hizmet ==============================

SERVICE_STATUS_HANDLE g_ssh = nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stop = nullptr, g_sessionChanged = nullptr;

void setState(DWORD state) {
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted =
        state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE : 0;
    g_status.dwWaitHint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : 15000;
    SetServiceStatus(g_ssh, &g_status);
}

DWORD WINAPI controlHandler(DWORD ctrl, DWORD type, LPVOID, LPVOID) {
    switch (ctrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            setState(SERVICE_STOP_PENDING);
            SetEvent(g_stop);
            return NO_ERROR;
        case SERVICE_CONTROL_SESSIONCHANGE:
            if (type == WTS_CONSOLE_CONNECT || type == WTS_CONSOLE_DISCONNECT || type == WTS_SESSION_LOGON ||
                type == WTS_SESSION_LOGOFF)
                SetEvent(g_sessionChanged);
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

// Ajanı ekrandaki oturumda SYSTEM olarak başlatır. Hizmet kapanınca iş nesnesi ajanı da kapatır.
class AgentProcess {
public:
    AgentProcess() {
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &li, sizeof li);
        stop_ = CreateEventW(nullptr, TRUE, FALSE, kAgentStopEvent);
    }
    ~AgentProcess() {
        stop();
        CloseHandle(stop_);
        CloseHandle(job_);
    }

    HANDLE process() const { return proc_; }
    DWORD session() const { return session_; }

    bool start(DWORD sessionId) {
        HANDLE tok = nullptr, dup = nullptr;
        bool ok = OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &tok) &&
                  DuplicateTokenEx(tok, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &dup) &&
                  SetTokenInformation(dup, TokenSessionId, &sessionId, sizeof sessionId);
        PROCESS_INFORMATION pi{};
        if (ok) {
            const std::wstring exe = selfPath();
            std::wstring cmd = L"\"" + exe + L"\" --ajan";
            STARTUPINFOW si{};
            si.cb = sizeof si;
            si.lpDesktop = const_cast<wchar_t*>(L"winsta0\\default");
            ResetEvent(stop_);
            ok = CreateProcessAsUserW(dup, exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                                      CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        }
        const DWORD err = GetLastError();
        if (dup) CloseHandle(dup);
        if (tok) CloseHandle(tok);
        if (!ok) {
            logf("Ajan başlatılamadı (oturum %lu, hata %lu)", sessionId, err);
            return false;
        }
        AssignProcessToJobObject(job_, pi.hProcess);
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        proc_ = pi.hProcess;
        session_ = sessionId;
        startedAt_ = GetTickCount64();
        logf("Ajan başladı (oturum %lu, süreç %lu)", sessionId, pi.dwProcessId);
        return true;
    }

    // Önce kibarca (sanal monitör kaldırılsın, tek ekran düzeni geri gelsin), olmazsa zorla
    void stop() {
        if (!proc_) return;
        SetEvent(stop_);
        if (WaitForSingleObject(proc_, 10000) == WAIT_TIMEOUT) {
            logf("Ajan kapanmadı; sonlandırılıyor");
            TerminateProcess(proc_, 1);
            WaitForSingleObject(proc_, 3000);
        }
        reap();
    }

    // Süreç bitti: tanıtıcıyı kapat; ne kadar çalıştığını döndür (ms)
    uint64_t reap() {
        if (!proc_) return 0;
        DWORD code = 0;
        GetExitCodeProcess(proc_, &code);
        CloseHandle(proc_);
        proc_ = nullptr;
        const uint64_t ran = GetTickCount64() - startedAt_;
        if (code != 0) logf("Ajan çıktı (kod %lu, %llu sn çalıştı)", code, (unsigned long long)(ran / 1000));
        return ran;
    }

private:
    HANDLE job_ = nullptr, stop_ = nullptr, proc_ = nullptr;
    DWORD session_ = 0xFFFFFFFF;
    uint64_t startedAt_ = 0;
};

void WINAPI serviceMain(DWORD, LPWSTR*) {
    g_ssh = RegisterServiceCtrlHandlerExW(kName, controlHandler, nullptr);
    if (!g_ssh) return;
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_sessionChanged = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CreateDirectoryW(config::programDataDir().c_str(), nullptr);
    logToFile(config::programDataDir() + L"\\hizmet.log");
    logf("EKRAN hizmeti başladı (%ls)", config::kVersion);
    setState(SERVICE_RUNNING);

    {
        AgentProcess agent;
        DWORD backoffMs = 2000;
        for (;;) {
            const DWORD sid = WTSGetActiveConsoleSessionId();
            if (sid != 0xFFFFFFFF && agent.process() && agent.session() != sid) {
                logf("Ekrandaki oturum değişti: %lu → %lu", agent.session(), sid);
                agent.stop();
            }
            if (sid != 0xFFFFFFFF && !agent.process()) agent.start(sid);

            HANDLE hs[3] = {g_stop, g_sessionChanged, agent.process()};
            const DWORD n = agent.process() ? 3 : 2;
            const DWORD w = WaitForMultipleObjects(n, hs, FALSE, agent.process() ? INFINITE : 2000);
            if (w == WAIT_OBJECT_0) break;
            if (w == WAIT_OBJECT_0 + 2) {
                // Ajan kendiliğinden çıktı: bekleyerek yeniden başlat (sürekli çöküyorsa aralık uzar)
                const uint64_t ran = agent.reap();
                backoffMs = ran > 60000 ? 2000 : std::min<DWORD>(backoffMs * 2, 30000);
                if (WaitForSingleObject(g_stop, backoffMs) == WAIT_OBJECT_0) break;
            }
        }
        agent.stop();
    }
    logf("EKRAN hizmeti durdu");
    setState(SERVICE_STOPPED);
}

// ============================== Kurulum ==============================

bool createKey(const wchar_t* path, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};
    HKEY k;
    DWORD disp = 0;
    const LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_ALL_ACCESS, &sa, &k, &disp);
    if (r == ERROR_SUCCESS) {
        if (disp == REG_OPENED_EXISTING_KEY) RegSetKeySecurity(k, DACL_SECURITY_INFORMATION, sd);
        RegCloseKey(k);
    }
    LocalFree(sd);
    return r == ERROR_SUCCESS;
}

bool createDir(const std::wstring& path, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};
    bool ok = CreateDirectoryW(path.c_str(), &sa) || GetLastError() == ERROR_ALREADY_EXISTS;
    if (ok) SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, sd);
    LocalFree(sd);
    return ok;
}

// Çalışan bir exe'nin üstüne de yazabilmek için: kilitliyse eskisini kenara al (yeniden başlatınca silinir)
bool copyFile(const std::wstring& from, const std::wstring& to) {
    if (_wcsicmp(from.c_str(), to.c_str()) == 0) return true;
    if (CopyFileW(from.c_str(), to.c_str(), FALSE)) return true;
    const std::wstring old = to + L".eski";
    DeleteFileW(old.c_str());
    if (MoveFileExW(to.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        if (CopyFileW(from.c_str(), to.c_str(), FALSE)) return true;
    }
    logf("Kopyalanamadı: %ls (hata %lu)", to.c_str(), GetLastError());
    return false;
}

// Bir anahtarın tüm değerlerini diğerine kopyalar (üzerine yazar)
void copyValues(HKEY fromRoot, const std::wstring& from, HKEY toRoot, const std::wstring& to, bool overwrite) {
    HKEY src, dst;
    if (RegOpenKeyExW(fromRoot, from.c_str(), 0, KEY_READ, &src) != 0) return;
    if (RegCreateKeyExW(toRoot, to.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &dst, nullptr) != 0) {
        RegCloseKey(src);
        return;
    }
    for (DWORD i = 0;; i++) {
        wchar_t name[256];
        BYTE data[512];
        DWORD nameLen = ARRAYSIZE(name), dataLen = sizeof data, type = 0;
        const LONG r = RegEnumValueW(src, i, name, &nameLen, nullptr, &type, data, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        if (!overwrite && RegQueryValueExW(dst, name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) continue;
        RegSetValueExW(dst, name, 0, type, data, dataLen);
    }
    RegCloseKey(dst);
    RegCloseKey(src);
}

void deleteValue(HKEY root, const std::wstring& path, const wchar_t* name) {
    HKEY k;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE, &k) == 0) {
        RegDeleteValueW(k, name);
        RegCloseKey(k);
    }
}

void stopAndWait(SC_HANDLE svc) {
    SERVICE_STATUS st{};
    if (!QueryServiceStatus(svc, &st) || st.dwCurrentState == SERVICE_STOPPED) return;
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    for (int i = 0; i < 100; i++) {  // en fazla 20 sn
        if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_STOPPED) return;
        Sleep(200);
    }
}

std::wstring userProfile() {
    wchar_t p[MAX_PATH] = L"";
    SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, p);
    return p;
}

// SDDL: SY = SYSTEM, BA = Yöneticiler, BU = Kullanıcılar
constexpr const wchar_t* kKeyPublic = L"D:P(A;OICI;KA;;;SY)(A;OICI;KA;;;BA)(A;OICI;KR;;;BU)";
constexpr const wchar_t* kKeyUserWritable = L"D:P(A;OICI;KA;;;SY)(A;OICI;KA;;;BA)(A;OICI;KRKW;;;BU)";
constexpr const wchar_t* kKeyPrivate = L"D:P(A;OICI;KA;;;SY)(A;OICI;KA;;;BA)";
constexpr const wchar_t* kDirPublic = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)";
constexpr const wchar_t* kDirPrivate = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";

}  // namespace

// ============================== Dışarıdan ==============================

int runService() {
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t*>(kName), serviceMain}, {nullptr, nullptr}};
    return StartServiceCtrlDispatcherW(table) ? 0 : int(GetLastError());
}

std::wstring agentLogPath() { return config::programDataDir() + L"\\ekran.log"; }

int runAgent() {
    HANDLE single = CreateMutexW(nullptr, TRUE, kAgentMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    config::useMachineStore(true);
    enableDesktopFollowing(true);
    CreateDirectoryW(config::programDataDir().c_str(), nullptr);
    logToFile(agentLogPath());
    DWORD sid = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    logf("EKRAN ajanı başladı (oturum %lu, masaüstü %ls)", sid, inputDesktopName().c_str());

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // Tablete daha önce tanıtılmış adb anahtarı (kurulumda kullanıcınınkinden kopyalandı): SYSTEM'in
    // kendi anahtarı olsaydı tablet "USB hata ayıklamaya izin verilsin mi?" diye yeniden sorardı.
    const std::wstring key = config::programDataDir() + L"\\anahtar\\adbkey";
    if (exists(key)) SetEnvironmentVariableW(L"ADB_VENDOR_KEYS", key.c_str());

    HANDLE stopEv = OpenEventW(SYNCHRONIZE, FALSE, kAgentStopEvent);  // elle çalıştırılınca yok
    HKEY settings = nullptr;
    RegOpenKeyExW(HKEY_LOCAL_MACHINE, config::kSettingsKey, 0, KEY_NOTIFY | KEY_QUERY_VALUE, &settings);
    HANDLE regEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    config::writeStatus(SessionState{});

    auto handleForget = [] {
        if (config::get(L"ForgetPairs", 0) == 0) return false;
        pairing::forgetAll();
        config::set(L"ForgetPairs", 0);  // SYSTEM yazabilir
        logf("Tüm eşleşmeler silindi");
        return true;
    };
    handleForget();

    // Dil: ayar, yoksa tepsinin yazdığı kullanıcı dili (SYSTEM'in kendi dili kullanıcınınki değildir)
    auto applyLanguage = [] {
        const DWORD l = config::get(L"Language", 0);
        i18n::setLanguage(int(l ? l : config::get(L"UiLanguage", 0)));
    };
    applyLanguage();

    bool quit = false;
    while (!quit) {
        const Options opt = config::loadOptions();
        StreamerEvents ev;
        ev.onState = [](const SessionState& s) { config::writeStatus(s); };
        ev.onNotice = [](Notice n, const std::string& arg) { config::writeNotice(int(n), arg); };
        ev.onPairRequest = [](const std::string& device, const std::string& code, HANDLE cancel) {
            // Oturum açılmadan (kilit ekranı) yeni tablet eşleştirilmez: kilit ekranı yabancıya gitmesin
            const std::wstring desk = inputDesktopName();
            if (_wcsicmp(desk.c_str(), L"Default") != 0) {
                logf("Oturum kilitliyken eşleştirme yapılamaz (%s)", device.c_str());
                return false;
            }
            return askPairing(widen(device), widen(code), cancel);
        };
        // Tanı (Ayarlar\Tani = 1): saniyelik ölçümü günlüğe yaz
        if (config::get(L"Tani", 0))
            ev.onStats = [](const std::string& line) { logf("%s  [%ls]", line.c_str(), inputDesktopName().c_str()); };
        Streamer streamer(opt, ev);
        DWORD lastConnectNow = config::get(L"ConnectNow", 0);
        std::thread worker([&] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // STA gizli pencere açar; masaüstü değişemezdi
            streamer.run();
            CoUninitialize();
        });

        for (;;) {
            if (settings) RegNotifyChangeKeyValue(settings, FALSE, REG_NOTIFY_CHANGE_LAST_SET, regEv, TRUE);
            HANDLE hs[2] = {regEv, stopEv};
            const DWORD w = WaitForMultipleObjects(stopEv ? 2 : 1, hs, FALSE, settings ? INFINITE : 5000);
            if (w == WAIT_OBJECT_0 + 1) {
                quit = true;
                break;
            }
            if (w != WAIT_OBJECT_0) continue;
            if (handleForget()) continue;
            // Tepsideki "Şimdi bağlan": sayaç artar (kullanıcı HKLM'e yalnızca ayar yazabilir)
            const DWORD cn = config::get(L"ConnectNow", 0);
            if (cn != lastConnectNow) {
                lastConnectNow = cn;
                streamer.connectNow();
                continue;
            }
            // Ayarlar bağlantıyı koparmadan uygulanır (Streamer::update)
            streamer.update(config::loadOptions());
            applyLanguage();
        }
        streamer.stop();
        worker.join();
    }

    config::writeStatus(SessionState{});
    logf("EKRAN ajanı kapandı");
    if (settings) RegCloseKey(settings);
    CloseHandle(regEv);
    if (stopEv) CloseHandle(stopEv);
    WSACleanup();
    MFShutdown();
    CoUninitialize();
    CloseHandle(single);
    return 0;
}

bool installed() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, kName, SERVICE_QUERY_STATUS);
    const bool ok = svc != nullptr;
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

std::wstring installedVersion() {
    wchar_t v[32];
    DWORD size = sizeof v;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, config::kMachineKey, L"Version", RRF_RT_REG_SZ, nullptr, v, &size) != 0)
        return {};
    return v;
}

int install() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CreateDirectoryW(config::programDataDir().c_str(), nullptr);
    createDir(config::programDataDir(), kDirPublic);
    logToFile(config::programDataDir() + L"\\hizmet.log");
    logf("Hizmet kuruluyor (%ls)", config::kVersion);

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return int(GetLastError());
    SC_HANDLE svc = OpenServiceW(scm, kName, SERVICE_ALL_ACCESS);
    if (svc) stopAndWait(svc);

    // 1) Dosyalar: C:\Program Files\EKRAN (hizmet sabit bir yoldan çalışmalı; zip klasörü silinebilir)
    const std::wstring dir = installDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!copyFile(selfPath(), dir + L"\\ekran.exe")) return 2;
    // Eski sürümün kopyaladığı tablet uygulaması: PC artık tablete uygulama kurmuyor
    for (const wchar_t* f : {L"\\ekran-android.apk", L"\\ekran-android.version"}) DeleteFileW((dir + f).c_str());
    const std::wstring adbPath = adb::find();
    if (!adbPath.empty()) {
        CreateDirectoryW((dir + L"\\adb").c_str(), nullptr);
        for (const wchar_t* f : {L"adb.exe", L"AdbWinApi.dll", L"AdbWinUsbApi.dll"})
            if (exists(dirOf(adbPath) + L"\\" + f)) copyFile(dirOf(adbPath) + L"\\" + f, dir + L"\\adb\\" + f);
    } else {
        logf("adb bulunamadı: hizmet yalnızca Wi-Fi ile bağlanabilir");
    }

    // 2) Kayıt defteri: izinler + kullanıcının ayarlarını ve eşleşmelerini taşı (tablet yeniden sormasın)
    createKey(config::kMachineKey, kKeyPublic);
    createKey(config::kSettingsKey, kKeyUserWritable);
    createKey(config::kStatusKey, kKeyPublic);
    createKey(config::kSecretKey, kKeyPrivate);
    copyValues(HKEY_CURRENT_USER, config::kUserKey, HKEY_LOCAL_MACHINE, config::kSettingsKey, false);
    deleteValue(HKEY_LOCAL_MACHINE, config::kSettingsKey, L"PcId");  // ayar değil, gizli bölüme
    copyValues(HKEY_CURRENT_USER, config::kUserKey, HKEY_LOCAL_MACHINE, config::kSecretKey, false);
    copyValues(HKEY_CURRENT_USER, std::wstring(config::kUserKey) + L"\\Pairs", HKEY_LOCAL_MACHINE,
               std::wstring(config::kSecretKey) + L"\\Pairs", false);
    {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, config::kMachineKey, 0, KEY_SET_VALUE, &k) == 0) {
            const std::wstring v = config::kVersion;
            RegSetValueExW(k, L"Version", 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                           DWORD((v.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
    }

    // 3) Kullanıcının adb anahtarı (tablet buna "her zaman izin ver" demişti); yalnızca SYSTEM okuyabilir
    const std::wstring keyDir = config::programDataDir() + L"\\anahtar";
    createDir(keyDir, kDirPrivate);
    for (const wchar_t* f : {L"adbkey", L"adbkey.pub"}) {
        const std::wstring from = userProfile() + L"\\.android\\" + f;
        if (exists(from)) copyFile(from, keyDir + L"\\" + f);
    }

    // 4) Hizmet: açılışta, oturum açılmadan başlar
    const std::wstring bin = L"\"" + dir + L"\\ekran.exe\" --hizmet";
    if (!svc) {
        svc = CreateServiceW(scm, kName, L"EKRAN tablet ekranı", SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                             SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, bin.c_str(), nullptr, nullptr, nullptr,
                             nullptr, nullptr);
        if (!svc) {
            const DWORD e = GetLastError();
            logf("Hizmet oluşturulamadı (%lu)", e);
            CloseServiceHandle(scm);
            return int(e);
        }
    } else {
        ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE, bin.c_str(), nullptr,
                             nullptr, nullptr, nullptr, nullptr, nullptr);
    }
    SERVICE_DESCRIPTIONW desc{const_cast<wchar_t*>(
        L"Eski tabletleri bu bilgisayarın ekranı yapar; oturum açma ekranı ve UAC dahil.")};
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);
    SC_ACTION actions[3] = {{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 30000}};
    SERVICE_FAILURE_ACTIONSW fa{};
    fa.dwResetPeriod = 3600;
    fa.cActions = 3;
    fa.lpsaActions = actions;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);

    const bool started = StartServiceW(svc, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    const DWORD e = started ? 0 : GetLastError();
    logf(started ? "Hizmet kuruldu ve başladı" : "Hizmet kuruldu ama başlamadı (%lu)", e);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return started ? 0 : int(e);
}

int uninstall() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return int(GetLastError());
    SC_HANDLE svc = OpenServiceW(scm, kName, SERVICE_ALL_ACCESS);
    if (svc) {
        stopAndWait(svc);  // ajan sanal monitörü kaldırır, tek ekran düzenini geri yükler
        DeleteService(svc);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);

    // Eşleşmeleri ve ayarları kullanıcıya geri taşı: tepsi uygulaması kaldığı yerden sürsün
    copyValues(HKEY_LOCAL_MACHINE, config::kSecretKey, HKEY_CURRENT_USER, config::kUserKey, true);
    copyValues(HKEY_LOCAL_MACHINE, std::wstring(config::kSecretKey) + L"\\Pairs", HKEY_CURRENT_USER,
               std::wstring(config::kUserKey) + L"\\Pairs", true);
    copyValues(HKEY_LOCAL_MACHINE, config::kSettingsKey, HKEY_CURRENT_USER, config::kUserKey, true);
    deleteValue(HKEY_CURRENT_USER, config::kUserKey, L"ForgetPairs");
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, config::kMachineKey);

    // Dosyalar (çalışan exe yeniden başlatınca silinir); günlükler kalır, özel anahtar silinir
    const std::wstring keyDir = config::programDataDir() + L"\\anahtar";
    for (const wchar_t* f : {L"adbkey", L"adbkey.pub"}) DeleteFileW((keyDir + L"\\" + f).c_str());
    RemoveDirectoryW(keyDir.c_str());
    const std::wstring dir = installDir();
    for (const wchar_t* f : {L"adb\\adb.exe", L"adb\\AdbWinApi.dll", L"adb\\AdbWinUsbApi.dll", L"ekran-android.apk",
                             L"ekran-android.version", L"ekran.exe"}) {
        const std::wstring p = dir + L"\\" + f;
        if (exists(p) && !DeleteFileW(p.c_str())) MoveFileExW(p.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    RemoveDirectoryW((dir + L"\\adb").c_str());
    if (!RemoveDirectoryW(dir.c_str())) MoveFileExW(dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}

bool runElevated(const std::wstring& args, DWORD& exitCode) {
    const std::wstring exe = selfPath();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false;
    WaitForSingleObject(sei.hProcess, INFINITE);
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return true;
}

}  // namespace ekran::service
