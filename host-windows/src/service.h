// "Oturum açılmadan önce başlat": EKRAN Windows hizmeti.
//
//   hizmet (Oturum 0, SYSTEM)  →  ekrandaki oturumda ajanı başlatır, çökerse yeniden başlatır,
//                                  kullanıcı değişince yeni oturuma taşır
//   ajan   (--ajan, SYSTEM, kullanıcının oturumunda)  →  yayını yapar; kilit ekranı / oturum açma / UAC
//                                  masaüstüne geçebilir, oraya da dokunma iletir
//   tepsi  (kullanıcı)  →  hizmet kuruluysa yalnızca arayüz: ayarları HKLM'e yazar, durumu oradan okur
#pragma once

#include <windows.h>

#include <string>

namespace ekran::service {

constexpr const wchar_t* kName = L"EKRAN";

int runService();  // --hizmet (hizmet denetleyicisi başlatır)
int runAgent();    // --ajan
int install();     // --hizmet-kur (yönetici)
int uninstall();   // --hizmet-kaldir (yönetici)

bool installed();
std::wstring installedVersion();
std::wstring agentLogPath();

// Kendini yönetici olarak (UAC) çalıştırıp bitmesini bekler. Kullanıcı vazgeçerse false.
bool runElevated(const std::wstring& args, DWORD& exitCode);

}  // namespace ekran::service
