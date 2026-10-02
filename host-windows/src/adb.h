// USB bağlantısı için adb yardımcıları (Android 4.x'te adb reverse yok → adb forward).
#pragma once

#include <string>

namespace ekran::adb {

// ANDROID_HOME, ANDROID_SDK_ROOT, %LOCALAPPDATA%\Android\Sdk, PATH sırasıyla aranır.
std::wstring find();

// Komutu pencere açmadan çalıştırır; çıktıyı döndürür. Başarısızsa false.
bool run(const std::wstring& adbPath, const std::wstring& args, std::string* output = nullptr, int timeoutMs = 10000);

// USB'de hata ayıklaması açık cihazın seri numarası; yoksa boş (sessiz; tek adb çağrısı)
std::string serial(const std::wstring& adbPath);

// Tablette EKRAN uygulaması kurulu mu. PC uygulama kurmaz: APK ayrıca indirilip tablete kurulur.
enum class App { Installed, Missing, Unknown };
App appState(const std::wstring& adbPath);

// Port yönlendir (adb forward)
bool prepareUsb(const std::wstring& adbPath, unsigned port);

// Tableti ekran olmaya davet et: kararı tablet verir ("bilgisayara takılınca otomatik aç" ayarı).
enum class Launch { Started, AlreadyOpen, Declined, Failed };
Launch requestLaunch(const std::wstring& adbPath);

// Tablet ekranını hemen kapat (Windows ekranları kapatınca). Tablet "USB takılıyken uyanık kal"
// ayarındaysa kendi zaman aşımı hiç işlemez; bu yüzden USB'de PC kapatır. Uyandırmayı tablet yapar.
void screenOff(const std::wstring& adbPath, int sdk);

}  // namespace ekran::adb
