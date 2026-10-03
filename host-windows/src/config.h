// Ayarların ve eşleşme anahtarlarının yeri.
//  - Kullanıcı (tepsi, konsol): HKCU\Software\EKRAN, eşleşmeler HKCU\Software\EKRAN\Pairs
//  - Makine (hizmet/ajan; oturum açılmadan önce de çalışır):
//      HKLM\SOFTWARE\EKRAN\Ayarlar   ayarlar (kullanıcılar okuyup yazabilir: tepsi menüsü)
//      HKLM\SOFTWARE\EKRAN\Durum     bağlantı durumu (ajan yazar, tepsi okur)
//      HKLM\SOFTWARE\EKRAN\Gizli     PC kimliği ve eşleşmeler (yalnızca SYSTEM ve yöneticiler)
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "streamer.h"

namespace ekran::config {

constexpr const wchar_t* kVersion = L"0.4.1";

constexpr const wchar_t* kUserKey = L"Software\\EKRAN";
constexpr const wchar_t* kMachineKey = L"SOFTWARE\\EKRAN";
constexpr const wchar_t* kSettingsKey = L"SOFTWARE\\EKRAN\\Ayarlar";
constexpr const wchar_t* kStatusKey = L"SOFTWARE\\EKRAN\\Durum";
constexpr const wchar_t* kSecretKey = L"SOFTWARE\\EKRAN\\Gizli";

void useMachineStore(bool on);
bool machineStore();

// Ayarlar (DWORD)
DWORD get(const wchar_t* name, DWORD def);
void set(const wchar_t* name, DWORD v);
Options loadOptions();
// Monitör yönü. "Orientation": 0 tabletle döner, 1–4 sabit 0/90/180/270°.
// "LastRotation": otomatikte en son kullanılan yön (tablet yön bildirmezse bununla açılır).
void saveRotation(int degrees);  // -1: otomatik
int lastRotation();              // yoksa 0
void saveLastRotation(int degrees);
// "Resolution": ikinci ekranın çözünürlüğü, tabletinkinin yüzdesi (kResolutions)
void saveResolution(int percent);

// Eşleşme deposu: kök anahtar ve yol (pairing.cpp)
HKEY secretRoot();
std::wstring secretPath();   // PcId burada
std::wstring pairsPath();    // cihaz kimliği → anahtar

// Wi-Fi ile bağlanılan son tablet adresleri (en yenisi önce, en çok 4). Keşifte bunlara doğrudan da sorulur:
// Wi-Fi yayını güvenilir değil (onaysız, güç tasarrufundaki tablet kaçırabilir), doğrudan paket ulaşır.
std::vector<std::string> knownAddresses();
void rememberAddress(const std::string& ip);

// Durum (ajan yazar, tepsi okur)
void writeStatus(const SessionState& s);
bool readStatus(SessionState& s);
DWORD statusSeq();  // her writeStatus'ta artar (bağlantı ya da görüntü modu değişti)
// Kullanıcıya gösterilecek uyarı (ajan yazar, tepsi kendi dilinde bildirim olarak gösterir).
// code: Notice (streamer.h). Her yazışta numara artar.
void writeNotice(int code, const std::string& arg);
DWORD noticeId();
DWORD readNotice(int& code, std::string& arg);  // numarayı döndürür

// %ProgramData%\EKRAN
std::wstring programDataDir();

}  // namespace ekran::config
