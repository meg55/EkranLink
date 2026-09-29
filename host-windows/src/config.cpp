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
    return o;
}

HKEY secretRoot() { return g_machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }
std::wstring secretPath() { return g_machine ? kSecretKey : kUserKey; }
std::wstring pairsPath() { return secretPath() + L"\\Pairs"; }

void writeStatus(bool connected, const std::string& device, const std::string& link) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kStatusKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != 0) return;
    const DWORD c = connected ? 1 : 0;
    // Önce ad ve bağlantı, en son "Connected": okuyan taraf tek bildirimde tutarlı değerleri görsün
    setString(k, L"Device", widen(device));
    setString(k, L"Link", widen(link));
    RegSetValueExW(k, L"Connected", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&c), sizeof c);
    RegCloseKey(k);
}

bool readStatus(bool& connected, std::wstring& device, std::wstring& link) {
    DWORD c = 0, size = sizeof c;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kStatusKey, L"Connected", RRF_RT_REG_DWORD, nullptr, &c, &size) != 0)
        return false;
    connected = c != 0;
    device = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"Device");
    link = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"Link");
    return true;
}

std::wstring programDataDir() {
    // SHGetKnownFolderPath değil: FOLDERID_* sabitleri libuuid'in 80 KB'lık nesnesini exe'ye çeker
    wchar_t p[MAX_PATH] = L"C:\\ProgramData";
    SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, p);
    return std::wstring(p) + L"\\EKRAN";
}

}  // namespace ekran::config
