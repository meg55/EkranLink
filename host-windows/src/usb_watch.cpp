#include "usb_watch.h"

#include <cfgmgr32.h>

namespace ekran {

namespace {

// Android ADB arabirim sınıfı (Google USB sürücüsü, üretici sürücüleri de aynı sınıfı kullanır)
constexpr GUID kAdbInterface = {0xF72FE0D4, 0xCBCB, 0x407D, {0x88, 0x14, 0x9E, 0xD6, 0x73, 0xD0, 0xDD, 0x6B}};

DWORD CALLBACK onDevice(HCMNOTIFICATION, PVOID ctx, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD) {
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)
        SetEvent(static_cast<HANDLE>(ctx));
    return ERROR_SUCCESS;
}

}  // namespace

UsbWatch::UsbWatch() {
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CM_NOTIFY_FILTER f{};
    f.cbSize = sizeof f;
    f.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    f.u.DeviceInterface.ClassGuid = kAdbInterface;
    HCMNOTIFICATION h = nullptr;
    if (CM_Register_Notification(&f, event_, onDevice, &h) == CR_SUCCESS) notify_ = h;
}

UsbWatch::~UsbWatch() {
    if (notify_) CM_Unregister_Notification(static_cast<HCMNOTIFICATION>(notify_));
    CloseHandle(event_);
}

}  // namespace ekran
