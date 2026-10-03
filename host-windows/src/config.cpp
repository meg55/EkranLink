#include "config.h"

#include <shlobj.h>

#include <atomic>

namespace ekran::config {

namespace {

std::atomic<bool> g_machine{false};

HKEY settingsRoot() { return g_machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }
const wchar_t* settingsPath() { return g_machine ? kSettingsKey : kUserKey; }

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

void setString(HKEY k, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), DWORD((v.size() + 1) * sizeof(wchar_t)));
}

std::wstring getString(HKEY root, const wchar_t* path, const wchar_t* name) {
    wchar_t buf[256];
    DWORD size = sizeof buf;
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}

}  // namespace

void useMachineStore(bool on) { g_machine = on; }
bool machineStore() { return g_machine; }

DWORD get(const wchar_t* name, DWORD def) {
    DWORD v = def, size = sizeof v;
    if (RegGetValueW(settingsRoot(), settingsPath(), name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return def;
    return v;
}

void set(const wchar_t* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(settingsRoot(), settingsPath(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == 0) {
        RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v);
        RegCloseKey(k);
    }
}

Options loadOptions() {
    Options o;
    o.mode = LinkMode(get(L"Mode", DWORD(LinkMode::Auto)));
    o.output = int(get(L"Output", 0));
    o.extend = get(L"Mirror", 0) == 0;
    o.onlyTablet = get(L"OnlyTablet", 0) != 0;
    o.touchAsMouse = get(L"TouchAsMouse", 0) != 0;
    o.lowLatency = get(L"LowLatency", 0) != 0;
    o.autoConnect = get(L"AutoConnect", 1) != 0;
    const DWORD r = get(L"Orientation", 0);
    o.rotation = r >= 1 && r <= 4 ? int(r - 1) * 90 : -1;
    return o;
}

void saveRotation(int degrees) { set(L"Orientation", degrees < 0 ? 0 : DWORD(degrees / 90 % 4 + 1)); }

int lastRotation() {
    const DWORD r = get(L"LastRotation", 0);
    return r >= 1 && r <= 4 ? int(r - 1) * 90 : 0;
}

void saveLastRotation(int degrees) {
    if (lastRotation() != degrees) set(L"LastRotation", DWORD(degrees / 90 % 4 + 1));
}

HKEY secretRoot() { return g_machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }
std::wstring secretPath() { return g_machine ? kSecretKey : kUserKey; }
std::wstring pairsPath() { return secretPath() + L"\\Pairs"; }

std::vector<std::string> knownAddresses() {
    std::vector<std::string> out;
    const std::wstring w = getString(secretRoot(), secretPath().c_str(), L"Tabletler");
    size_t b = 0;
    while (b < w.size()) {
        size_t e = w.find(L' ', b);
        if (e == std::wstring::npos) e = w.size();
        if (e > b) out.emplace_back(w.begin() + b, w.begin() + e);  // yalnızca ASCII (IPv4)
        b = e + 1;
    }
    return out;
}

void rememberAddress(const std::string& ip) {
    const std::vector<std::string> list = knownAddresses();
    if (!list.empty() && list.front() == ip) return;
    std::wstring w = widen(ip);
    int n = 1;
    for (const auto& a : list)
        if (a != ip && n++ < 4) w += L" " + widen(a);
    HKEY k;
    if (RegCreateKeyExW(secretRoot(), secretPath().c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != 0) return;
    setString(k, L"Tabletler", w);
    RegCloseKey(k);
}

void writeStatus(bool connected, const std::string& device, const std::string& link, int mode) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kStatusKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != 0) return;
    const DWORD c = connected ? 1 : 0, m = DWORD(mode);
    // Önce ad, bağlantı ve mod, en son "Connected": okuyan taraf tek bildirimde tutarlı değerleri görsün
    setString(k, L"Device", widen(device));
    setString(k, L"Link", widen(link));
    RegSetValueExW(k, L"Display", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&m), sizeof m);
    RegSetValueExW(k, L"Connected", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&c), sizeof c);
    RegCloseKey(k);
}

bool readStatus(bool& connected, std::wstring& device, std::wstring& link, int& mode) {
    DWORD c = 0, m = 0, size = sizeof c;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kStatusKey, L"Connected", RRF_RT_REG_DWORD, nullptr, &c, &size) != 0)
        return false;
    size = sizeof m;
    RegGetValueW(HKEY_LOCAL_MACHINE, kStatusKey, L"Display", RRF_RT_REG_DWORD, nullptr, &m, &size);
    connected = c != 0;
    mode = int(m);
    device = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"Device");
    link = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"Link");
    return true;
}

void writeNotice(int code, const std::string& arg) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kStatusKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != 0) return;
    const DWORD c = DWORD(code);
    RegSetValueExW(k, L"NoticeCode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&c), sizeof c);
    setString(k, L"NoticeArg", widen(arg));
    const DWORD id = noticeId() + 1;  // en son: okuyan taraf değerleri hazır bulsun
    RegSetValueExW(k, L"NoticeId", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&id), sizeof id);
    RegCloseKey(k);
}

DWORD noticeId() {
    DWORD id = 0, size = sizeof id;
    RegGetValueW(HKEY_LOCAL_MACHINE, kStatusKey, L"NoticeId", RRF_RT_REG_DWORD, nullptr, &id, &size);
    return id;
}

DWORD readNotice(int& code, std::string& arg) {
    const DWORD id = noticeId();
    DWORD c = 0, size = sizeof c;
    RegGetValueW(HKEY_LOCAL_MACHINE, kStatusKey, L"NoticeCode", RRF_RT_REG_DWORD, nullptr, &c, &size);
    code = int(c);
    const std::wstring w = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"NoticeArg");
    arg.assign(w.begin(), w.end());  // sürüm metni (ASCII)
    return id;
}

std::wstring programDataDir() {
    // SHGetKnownFolderPath değil: FOLDERID_* sabitleri libuuid'in 80 KB'lık nesnesini exe'ye çeker
    wchar_t p[MAX_PATH] = L"C:\\ProgramData";
    SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, p);
    return std::wstring(p) + L"\\EKRAN";
}

}  // namespace ekran::config
