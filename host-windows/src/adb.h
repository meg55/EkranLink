// USB bağlantısı için adb yardımcıları (Android 4.x'te adb reverse yok → adb forward).
#pragma once

#include <string>
#include <vector>

namespace ekran::adb {

// ANDROID_HOME, ANDROID_SDK_ROOT, %LOCALAPPDATA%\Android\Sdk, PATH sırasıyla aranır.
std::wstring find();

// Komutu pencere açmadan çalıştırır; çıktıyı döndürür. Başarısızsa false.
bool run(const std::wstring& adbPath, const std::wstring& args, std::string* output = nullptr, int timeoutMs = 10000);

// USB'de hata ayıklaması açık (yetkili) cihazların seri numaraları (sessiz; tek adb çağrısı).
// Sonraki komutlar seri numarasıyla o cihaza gider.
std::vector<std::string> devices(const std::wstring& adbPath);

// Cihazda EKRAN uygulaması kurulu mu. PC uygulama kurmaz: APK ayrıca indirilip cihaza kurulur.
enum class App { Installed, Missing, Unknown };
App appState(const std::wstring& adbPath, const std::string& serial);

// Port yönlendir (adb forward)
bool prepareUsb(const std::wstring& adbPath, const std::string& serial, unsigned port);

// Cihazı ekran olmaya davet et: kararı cihaz verir ("bilgisayara takılınca otomatik aç" ayarı).
enum class Launch { Started, AlreadyOpen, Declined, Failed };
Launch requestLaunch(const std::wstring& adbPath, const std::string& serial);

// Cihaz ekranını hemen kapat (Windows ekranları kapatınca). Cihaz "USB takılıyken uyanık kal"
// ayarındaysa kendi zaman aşımı hiç işlemez; bu yüzden USB'de PC kapatır. Uyandırmayı cihaz yapar.
void screenOff(const std::wstring& adbPath, const std::string& serial, int sdk);

}  // namespace ekran::adb
