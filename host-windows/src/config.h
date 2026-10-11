// Ayarların ve eşleşme anahtarlarının yeri.
//  - Kullanıcı (tepsi, konsol): HKCU\Software\EkranLink, eşleşmeler HKCU\Software\EkranLink\Pairs
//  - Makine (hizmet/ajan; oturum açılmadan önce de çalışır):
//      HKLM\SOFTWARE\EkranLink\Ayarlar   ayarlar (kullanıcılar okuyup yazabilir: tepsi menüsü)
//      HKLM\SOFTWARE\EkranLink\Durum     bağlantı durumu (ajan yazar, tepsi okur)
//      HKLM\SOFTWARE\EkranLink\Gizli     PC kimliği ve eşleşmeler (yalnızca SYSTEM ve yöneticiler)
#pragma once

#include <windows.h>

#include <map>
#include <string>
#include <vector>

#include "streamer.h"
#include "version.h"

namespace ekran::config {

constexpr const wchar_t* kVersion = L"" EKRANLINK_VERSION;

constexpr const wchar_t* kUserKey = L"Software\\EkranLink";
constexpr const wchar_t* kMachineKey = L"SOFTWARE\\EkranLink";
constexpr const wchar_t* kSettingsKey = L"SOFTWARE\\EkranLink\\Ayarlar";
constexpr const wchar_t* kStatusKey = L"SOFTWARE\\EkranLink\\Durum";
constexpr const wchar_t* kSecretKey = L"SOFTWARE\\EkranLink\\Gizli";

void useMachineStore(bool on);
bool machineStore();

// Ayarlar (DWORD)
DWORD get(const wchar_t* name, DWORD def);
void set(const wchar_t* name, DWORD v);
Options loadOptions();
// Monitör yönü. "Orientation": 0 tabletle döner, 1–4 sabit 0/90/180/270°.
// "SonYon-<cihaz>": otomatikte o cihazın en son kullanılan yönü (cihaz yön bildirmezse bununla açılır).
void saveRotation(int degrees);  // -1: otomatik
int lastRotation(const std::string& deviceId);  // yoksa 0
void saveLastRotation(const std::string& deviceId, int degrees);
// İkinci ekranın çözünürlüğü, cihaz başına ("Cozunurluk-<cihaz>" = "1600x940", yatay yöndeki boyut).
// Yoksa cihazın kendi çözünürlüğü (sürücünün önerdiği mod) kullanılır.
bool resolutionFor(const std::string& deviceId, int& width, int& height);
void saveResolution(const std::string& deviceId, int width, int height);

// Eşleşme deposu: kök anahtar ve yol (pairing.cpp)
HKEY secretRoot();
std::wstring secretPath();   // PcId burada
std::wstring pairsPath();    // cihaz kimliği → anahtar

// Wi-Fi ile bağlanılan son tablet adresleri (en yenisi önce, en çok 4). Keşifte bunlara doğrudan da sorulur:
// Wi-Fi yayını güvenilir değil (onaysız, güç tasarrufundaki tablet kaçırabilir), doğrudan paket ulaşır.
std::vector<std::string> knownAddresses();
void rememberAddress(const std::string& ip);

// Cihaz listesi (ajan yazar, tepsi okur): "Cihazlar" (REG_MULTI_SZ, satır başına bir cihaz), "Connected"
// (bağlı cihaz sayısı) ve ilk bağlı cihazın ayrıntıları (Device, Link, ...: tanı betikleri için)
void writeDevices(const std::vector<DeviceInfo>& list);
bool readDevices(std::vector<DeviceInfo>& list);
DWORD statusSeq();  // her writeDevices'ta artar
// Saniyelik ölçüm satırları (cihaz anahtarı → satır): ajan yalnızca ayar penceresi açıkken yazar ("Izle" = 1)
void writeStats(const std::map<std::string, std::string>& byKey);
std::map<std::string, std::string> readStats();
// Tepsiden ajana komut ("baglan <anahtar>", "kes <anahtar>", "kes"): kullanıcı HKLM'e yalnızca ayar
// anahtarına yazabilir; komut "Komut", ardından sayaç "KomutNo" yazılır, ajan sayacı izler.
void sendCommand(const std::string& cmd);
DWORD commandId();
std::string readCommand();
// Kullanıcıya gösterilecek uyarı (ajan yazar, tepsi kendi dilinde bildirim olarak gösterir).
// code: Notice (streamer.h). Her yazışta numara artar.
void writeNotice(int code, const std::string& arg);
DWORD noticeId();
DWORD readNotice(int& code, std::string& arg);  // numarayı döndürür

// %ProgramData%\EkranLink
std::wstring programDataDir();

}  // namespace ekran::config
