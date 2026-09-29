#include "pairing.h"

#include <windows.h>

#include <vector>

namespace ekran::pairing {

namespace {

const wchar_t* kRoot = L"Software\\EKRAN";
const wchar_t* kPairs = L"Software\\EKRAN\\Pairs";

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

bool readBinary(const wchar_t* key, const wchar_t* name, void* out, DWORD size) {
    DWORD n = size;
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_BINARY, nullptr, out, &n) == 0 && n == size;
}

void writeBinary(const wchar_t* key, const wchar_t* name, const void* data, DWORD size) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == 0) {
        RegSetValueExW(k, name, 0, REG_BINARY, static_cast<const BYTE*>(data), size);
        RegCloseKey(k);
    }
}

}  // namespace

std::array<uint8_t, 16> pcId() {
    std::array<uint8_t, 16> id{};
    if (!readBinary(kRoot, L"PcId", id.data(), 16)) {
        crypto::random(id.data(), 16);
        writeBinary(kRoot, L"PcId", id.data(), 16);
    }
    return id;
}

std::string pcName() {
    wchar_t w[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = ARRAYSIZE(w);
    if (!GetComputerNameW(w, &n)) return "PC";
    int len = WideCharToMultiByte(CP_UTF8, 0, w, int(n), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, int(n), s.data(), len, nullptr, nullptr);
    return s;
}

bool load(const std::string& deviceId, crypto::Bytes32& key) {
    return readBinary(kPairs, widen(deviceId).c_str(), key.data(), 32);
}

void save(const std::string& deviceId, const crypto::Bytes32& key) {
    writeBinary(kPairs, widen(deviceId).c_str(), key.data(), 32);
}

void forget(const std::string& deviceId) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPairs, 0, KEY_SET_VALUE, &k) == 0) {
        RegDeleteValueW(k, widen(deviceId).c_str());
        RegCloseKey(k);
    }
}

void forgetAll() { RegDeleteTreeW(HKEY_CURRENT_USER, kPairs); }

}  // namespace ekran::pairing
