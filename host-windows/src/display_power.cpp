#include "display_power.h"

#include "log.h"

namespace ekran {

namespace {

// powrprof.h'deki tanımlar (MinGW başlıklarında yok; Windows 8+)
typedef DWORD(WINAPI* RegisterFn)(LPCGUID guid, DWORD flags, HANDLE recipient, PVOID* handle);
typedef DWORD(WINAPI* UnregisterFn)(PVOID handle);
constexpr DWORD kNotifyCallback = 2;  // DEVICE_NOTIFY_CALLBACK

// {6FE69556-704A-47A0-8F24-C28D936FDA47}
constexpr GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

HMODULE powrprof() {
    static HMODULE m = LoadLibraryExW(L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return m;
}

}  // namespace

ULONG CALLBACK DisplayPower::onChange(PVOID ctx, ULONG type, PVOID setting) {
    if (type != PBT_POWERSETTINGCHANGE || !setting) return 0;
    const auto* s = static_cast<const POWERBROADCAST_SETTING*>(setting);
    if (s->DataLength < sizeof(DWORD) || !IsEqualGUID(s->PowerSetting, kConsoleDisplayState)) return 0;
    const int v = int(*reinterpret_cast<const DWORD*>(s->Data));  // 0 kapalı, 1 açık, 2 kısılmış
    auto* self = static_cast<DisplayPower*>(ctx);
    if (self->state_.exchange(v) != v) logf("Windows ekranı %s", v == 0 ? "kapattı" : v == 2 ? "kıstı" : "açtı");
    return 0;
}

bool DisplayPower::start() {
    if (handle_) return true;
    HMODULE m = powrprof();
    auto reg = m ? reinterpret_cast<RegisterFn>(reinterpret_cast<void*>(GetProcAddress(m, "PowerSettingRegisterNotification"))) : nullptr;
    if (!reg) return false;
    params_[0] = reinterpret_cast<void*>(&DisplayPower::onChange);
    params_[1] = this;
    // Kayıt olunca şimdiki durum da hemen bildirilir
    return reg(&kConsoleDisplayState, kNotifyCallback, params_, &handle_) == ERROR_SUCCESS;
}

void DisplayPower::stop() {
    if (!handle_) return;
    auto unreg = reinterpret_cast<UnregisterFn>(reinterpret_cast<void*>(GetProcAddress(powrprof(), "PowerSettingUnregisterNotification")));
    if (unreg) unreg(handle_);
    handle_ = nullptr;
}

}  // namespace ekran
