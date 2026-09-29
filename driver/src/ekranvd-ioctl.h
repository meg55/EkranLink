// EKRAN sanal ekran sürücüsü — sürücü ile PC programı arasındaki komutlar.
// SudoVDA'nın (MIT) arayüzünden türetildi; kimlikler EKRAN'a özel (SudoVDA/Apollo ile çakışmasın).
// Bu dosya hem sürücüde (MSVC) hem host-windows'ta (MinGW) derlenir.
#pragma once

#include <windows.h>
#include <winioctl.h>
#include <stdint.h>

#ifdef __cplusplus
namespace EKRANVD
{
#endif

#define IOCTL_ADD_VIRTUAL_DISPLAY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_REMOVE_VIRTUAL_DISPLAY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_RENDER_ADAPTER CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_WATCHDOG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DRIVER_PING CTL_CODE(FILE_DEVICE_UNKNOWN, 0x888, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_PROTOCOL_VERSION CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8FF, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _EKRANVD_PROTOCOL_VERSION {
	uint8_t Major;
	uint8_t Minor;
	uint8_t Incremental;
	uint8_t Reserved;
} EKRANVD_PROTOCOL_VERSION;

// Komutlar değişince artır
static const EKRANVD_PROTOCOL_VERSION EkranVdProtocolVersion = { 1, 0, 0, 0 };

#define EKRANVD_HARDWARE_ID L"Root\\EKRAN\\VirtualDisplay"

// {b1a52822-f3e2-4092-b7ee-abebada70d27}
static const GUID EKRANVD_INTERFACE_GUID = { 0xb1a52822, 0xf3e2, 0x4092, { 0xb7, 0xee, 0xab, 0xeb, 0xad, 0xa7, 0x0d, 0x27 } };

typedef struct _VIRTUAL_DISPLAY_ADD_PARAMS {
	UINT Width;
	UINT Height;
	UINT RefreshRate;        // Hz (ör. 60) ya da mHz (ör. 60000)
	GUID MonitorGuid;        // aynı tablet için hep aynı: Windows ekran yerleşimini hatırlar
	CHAR DeviceName[14];     // EDID ürün adı (en fazla 13 karakter)
	CHAR SerialNumber[14];   // EDID seri no (en fazla 13 karakter)
	UINT16 WidthMm;          // fiziksel boyut: Windows doğru ölçeklemeyi (DPI) seçsin; 0 = bilinmiyor
	UINT16 HeightMm;
} VIRTUAL_DISPLAY_ADD_PARAMS, * PVIRTUAL_DISPLAY_ADD_PARAMS;

typedef struct _VIRTUAL_DISPLAY_REMOVE_PARAMS {
	GUID MonitorGuid;
} VIRTUAL_DISPLAY_REMOVE_PARAMS, * PVIRTUAL_DISPLAY_REMOVE_PARAMS;

typedef struct _VIRTUAL_DISPLAY_ADD_OUT {
	LUID AdapterLuid;
	UINT TargetId;
} VIRTUAL_DISPLAY_ADD_OUT, * PVIRTUAL_DISPLAY_ADD_OUT;

typedef struct _VIRTUAL_DISPLAY_SET_RENDER_ADAPTER_PARAMS {
	LUID AdapterLuid;
} VIRTUAL_DISPLAY_SET_RENDER_ADAPTER_PARAMS, * PVIRTUAL_DISPLAY_SET_RENDER_ADAPTER_PARAMS;

typedef struct _VIRTUAL_DISPLAY_GET_WATCHDOG_OUT {
	UINT Timeout;
	UINT Countdown;
} VIRTUAL_DISPLAY_GET_WATCHDOG_OUT, * PVIRTUAL_DISPLAY_GET_WATCHDOG_OUT;

typedef struct _VIRTUAL_DISPLAY_GET_PROTOCOL_VERSION_OUT {
	EKRANVD_PROTOCOL_VERSION Version;
} VIRTUAL_DISPLAY_GET_PROTOCOL_VERSION_OUT, * PVIRTUAL_DISPLAY_GET_PROTOCOL_VERSION_OUT;

#ifdef __cplusplus
} // namespace EKRANVD
#endif
