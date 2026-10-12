// USB bağlantısı için adb yardımcıları (Android 4.x'te adb reverse yok → adb forward).
#pragma once

#include <string>
#include <vector>

namespace ekran::adb {

// Exe'nin yanındaki (paketle gelen) adb, sonra ANDROID_HOME, ANDROID_SDK_ROOT, %LOCALAPPDATA%\Android\Sdk, PATH
// sırasıyla aranır. Hizmet / ajanda (makine deposu) yalnızca exe'nin yanındaki.
std::wstring find();

// Kablosuz hata ayıklama ile bağlı cihaz mı ("192.168.1.5:5555", "adb-...._adb-tls-connect._tcp").
// "emulator-5554" değildir.
bool isNetworkSerial(const std::string& serial);

// Komutu pencere açmadan çalıştırır; çıktıyı döndürür. Başarısızsa false.
bool run(const std::wstring& adbPath, const std::wstring& args, std::string* output = nullptr, int timeoutMs = 10000);

// USB'de hata ayıklaması açık (yetkili) cihazların seri numaraları (sessiz; tek adb çağrısı).
// Sonraki komutlar seri numarasıyla o cihaza gider; [A-Za-z0-9._:-] dışında karakter içerenler atlanır.
std::vector<std::string> devices(const std::wstring& adbPath);

// Cihazda EkranLink uygulaması kurulu mu. PC uygulama kurmaz: APK ayrıca indirilip cihaza kurulur.
enum class App { Installed, Missing, Unknown };
App appState(const std::wstring& adbPath, const std::string& serial);

// Cihazdaki uygulamanın kimliği ve adı (uygulamayı açmadan; eski sürümler kimlik vermez, ad adb'den okunur).
// Birden çok cihazda aynı tableti USB ve Wi-Fi'de iki kez saymamak için.
App probe(const std::wstring& adbPath, const std::string& serial, std::string& id, std::string& name);

// Port yönlendir (adb forward): PC'deki localPort → cihazdaki remotePort. localPort 0 ise adb boş bir port
// seçer ve localPort'a yazılır (birden çok USB cihazı aynı anda bağlanabilsin).
bool prepareUsb(const std::wstring& adbPath, const std::string& serial, unsigned& localPort, unsigned remotePort);

// Cihazı ekran olmaya davet et: kararı cihaz verir ("bilgisayara takılınca otomatik aç" ayarı).
enum class Launch { Started, AlreadyOpen, Declined, Failed };
Launch requestLaunch(const std::wstring& adbPath, const std::string& serial);

// Cihaz ekranını hemen kapat (Windows ekranları kapatınca). Cihaz "USB takılıyken uyanık kal"
// ayarındaysa kendi zaman aşımı hiç işlemez; bu yüzden USB'de PC kapatır. Uyandırmayı cihaz yapar.
void screenOff(const std::wstring& adbPath, const std::string& serial, int sdk);

}  // namespace ekran::adb
