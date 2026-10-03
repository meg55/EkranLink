#include "config.h"

#include <shlobj.h>

#include <atomic>
#include <cctype>
#include <cwchar>

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

namespace {
std::wstring resolutionValue(const std::string& deviceId) {
    std::wstring name = L"Cozunurluk-";
    for (char c : deviceId.substr(0, 32))
        if (std::isalnum(static_cast<unsigned char>(c))) name += wchar_t(c);
    return name;
}
}  // namespace

bool resolutionFor(const std::string& deviceId, int& width, int& height) {
    const std::wstring v = getString(settingsRoot(), settingsPath(), resolutionValue(deviceId).c_str());
    return std::swscanf(v.c_str(), L"%dx%d", &width, &height) == 2 && width > 0 && height > 0;
}

void saveResolution(const std::string& deviceId, int width, int height) {
    int w = 0, h = 0;
    if (resolutionFor(deviceId, w, h) && w == width && h == height) return;
    HKEY k;
    if (RegCreateKeyExW(settingsRoot(), settingsPath(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != 0) return;
    wchar_t v[32];
    swprintf(v, 32, L"%dx%d", width, height);
    setString(k, resolutionValue(deviceId).c_str(), v);
    RegCloseKey(k);
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

void writeStatus(const SessionState& s) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kStatusKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != 0) return;
    auto dword = [&](const wchar_t* name, DWORD v) {
        RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v);
    };
    // Önce ayrıntılar, en son "Seq": okuyan taraf tek bildirimde tutarlı değerleri görsün
    setString(k, L"Device", widen(s.device));
    setString(k, L"Link", widen(s.link));
    setString(k, L"DeviceId", widen(s.deviceId));
    setString(k, L"Gdi", s.gdiName);
    dword(L"Display", DWORD(s.mode));
    dword(L"Width", DWORD(s.width));
    dword(L"Height", DWORD(s.height));
    dword(L"Connected", s.connected ? 1 : 0);
    DWORD seq = 0, size = sizeof seq;
    RegGetValueW(HKEY_LOCAL_MACHINE, kStatusKey, L"Seq", RRF_RT_REG_DWORD, nullptr, &seq, &size);
    dword(L"Seq", seq + 1);
    RegCloseKey(k);
}

bool readStatus(SessionState& s) {
    auto dword = [](const wchar_t* name, DWORD& v) {
        DWORD size = sizeof v;
        return RegGetValueW(HKEY_LOCAL_MACHINE, kStatusKey, name, RRF_RT_REG_DWORD, nullptr, &v, &size) == 0;
    };
    DWORD c = 0, m = 0, w = 0, h = 0;
    if (!dword(L"Connected", c)) return false;
    dword(L"Display", m);
    dword(L"Width", w);
    dword(L"Height", h);
    s.connected = c != 0;
    s.mode = DisplayMode(m);
    s.width = int(w);
    s.height = int(h);
    const std::wstring dev = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"Device");
    const std::wstring link = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"Link");
    auto narrow = [](const std::wstring& x) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, x.data(), int(x.size()), nullptr, 0, nullptr, nullptr);
        std::string out(size_t(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, x.data(), int(x.size()), out.data(), n, nullptr, nullptr);
        return out;
    };
    s.device = narrow(dev);
    s.link = narrow(link);
    s.deviceId = narrow(getString(HKEY_LOCAL_MACHINE, kStatusKey, L"DeviceId"));
    s.gdiName = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"Gdi");
    return true;
}

void writeStats(const std::string& line) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kStatusKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != 0) return;
    setString(k, L"Stats", widen(line));
    RegCloseKey(k);
}

std::string readStats() {
    const std::wstring w = getString(HKEY_LOCAL_MACHINE, kStatusKey, L"Stats");
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

DWORD statusSeq() {
    DWORD seq = 0, size = sizeof seq;
    RegGetValueW(HKEY_LOCAL_MACHINE, kStatusKey, L"Seq", RRF_RT_REG_DWORD, nullptr, &seq, &size);
    return seq;
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
