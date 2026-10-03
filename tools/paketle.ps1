<#
.SYNOPSIS
    İki ayrı indirme üretir: dist\EKRAN-<sürüm>-windows.zip ve dist\EKRAN-<sürüm>-android.apk

.DESCRIPTION
    Windows paketi: ekran.exe (bildirim alanı), ekran-host.exe (konsol), adb\ (Android SDK platform-tools'tan),
    BENIOKU.txt. Kurulum gerektirmez; zip'i açıp ekran.exe'yi çalıştırın.
    Tablet uygulaması pakette yok: PC tablete uygulama kurmaz, APK ayrıca indirilip tablete kurulur.

    Not: APK şimdilik hata ayıklama anahtarıyla imzalı. GitHub sürümleri için kalıcı bir sürüm anahtarı
    gerekecek (yoksa farklı makinede derlenen sürüm eskisinin üstüne kurulamaz).
#>
param([switch] $Temiz)

$ErrorActionPreference = 'Stop'
$kok = Split-Path -Parent $PSScriptRoot

# --- Sürüm (android/app/build.gradle) ---
$gradle = Get-Content (Join-Path $kok 'android\app\build.gradle') -Raw
$versionCode = [regex]::Match($gradle, 'versionCode\s+(\d+)').Groups[1].Value
$versionName = [regex]::Match($gradle, "versionName\s+'([^']+)'").Groups[1].Value
Write-Host "EKRAN $versionName (Android versionCode $versionCode)" -ForegroundColor Cyan
$kVersion = [regex]::Match((Get-Content (Join-Path $kok 'host-windows\src\config.h') -Raw), 'kVersion = L"([^"]+)"').Groups[1].Value
if ($kVersion -ne $versionName) { throw "Sürümler farklı: build.gradle $versionName, config.h $kVersion" }

# --- Android ---
if (-not $env:JAVA_HOME) {
    $jbr = 'C:\Program Files\Android\Android Studio\jbr'
    if (Test-Path $jbr) { $env:JAVA_HOME = $jbr }
}
Push-Location (Join-Path $kok 'android')
try {
    # --no-daemon: arkada kalan Gradle sunucusu yüzlerce MB bellek tutar ve çıktı borusunu açık bırakır
    & .\gradlew.bat assembleDebug --no-daemon --console=plain -q
    if ($LASTEXITCODE -ne 0) { throw 'Android derlemesi başarısız' }
} finally { Pop-Location }
$apk = Join-Path $kok 'android\app\build\outputs\apk\debug\app-debug.apk'

# --- Windows ---
$buildArgs = @{}
if ($Temiz) { $buildArgs.Temiz = $true }
& (Join-Path $kok 'host-windows\build.ps1') @buildArgs | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Windows derlemesi başarısız' }
$hostBuild = Join-Path $kok 'host-windows\build'
& (Join-Path $hostBuild 'ekran-test.exe') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Şifreleme testleri başarısız (ekran-test.exe)' }

# --- adb ---
$pt = $null
foreach ($aday in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, (Join-Path $env:LOCALAPPDATA 'Android\Sdk'))) {
    if ($aday -and (Test-Path (Join-Path $aday 'platform-tools\adb.exe'))) { $pt = Join-Path $aday 'platform-tools'; break }
}
if (-not $pt) { throw 'platform-tools bulunamadı (adb pakete eklenemiyor)' }

# --- Paket ---
$dist = Join-Path $kok 'dist'
$ad = "EKRAN-$versionName-windows"
$hedef = Join-Path $dist $ad
if (Test-Path $hedef) { Remove-Item -Recurse -Force $hedef }
New-Item -ItemType Directory -Force (Join-Path $hedef 'adb') | Out-Null

Copy-Item (Join-Path $hostBuild 'ekran.exe'), (Join-Path $hostBuild 'ekran-host.exe') $hedef
foreach ($f in 'adb.exe', 'AdbWinApi.dll', 'AdbWinUsbApi.dll', 'NOTICE.txt') {
    Copy-Item (Join-Path $pt $f) (Join-Path $hedef 'adb')
}
Copy-Item (Join-Path $kok 'LICENSE') $hedef

@"
EKRAN $versionName — eski tabletleri bilgisayar ekranı yapın
===========================================================

Kurulum gerekmez. ekran.exe'yi çalıştırın: ayar penceresi açılır, saatin yanında bir monitör simgesi
belirir. Pencereyi kapatmak EKRAN'ı kapatmaz; simgeye tıklayınca yeniden açılır (sağ tık: menü, çıkış).
Dil: pencerede Genel > Dil (Sistem / Türkçe / English). Tablet ve telefon cihazın dilini kullanır.

TABLET UYGULAMASI (ayrı indirilir)
  Tablete EKRAN-$versionName-android.apk dosyasını kurun: tabletin tarayıcısıyla indirip açın ya da
  USB ile tablete kopyalayıp dosya yöneticisinden açın. Tablet "bilinmeyen kaynaklardan yükleme"
  izni isteyebilir. İki uygulamanın sürümü uyuşmazsa ikisi de hangisinin güncelleneceğini söyler.
  USB ile bağlanmak için tablette Geliştirici seçenekleri > USB hata ayıklama açık olmalı.

KULLANIM
  Tablette EKRAN açıkken bilgisayar tableti kendiliğinden bulur (önce USB, yoksa Wi-Fi).
  Wi-Fi ile ilk bağlantıda iki cihazda aynı 6 haneli kod görünür; ikisinde de onaylayın.
  Tablet Windows'a çoklu dokunmatik ekran olarak bağlanır. Üç parmakla dokunmak ölçümleri gösterir.

GÖRÜNTÜ MODLARI (pencerede Görüntü)
  İkinci ekran  Tablet ayrı bir monitör olur.
  Tek ekran     Tablet bağlıyken diğer monitörler kapanır; tablet ayrılınca geri gelir.
  Yansıtma      Seçili PC ekranının kopyası.
  İkinci ekran ve tek ekran için EKRAN sanal ekran sürücüsü gerekir. Şimdilik yalnızca kaynak koddan
  derlenip kurulabiliyor (driver\README.md); sürücü yoksa EKRAN yansıtır.
  Windows ekranı kapatınca tablet de kapanır, açınca birlikte uyanır.
  Monitör yönü: Otomatik'te tableti dik çevirmek monitörü de dikey yapar (tablette "Otomatik döndür"
  açık olmalı). Yatay / Dikey / ters seçilebilir. Seçim ve Windows ekran ayarlarından yapılan
  değişiklik hatırlanır.
  Çözünürlük: tabletinkinin %50-150'si (yüksek = daha çok alan, küçük yazı). Windows ekran
  ayarlarından yapılan değişiklik de hatırlanır.
  Ayarlar bağlantıyı koparmadan uygulanır; tablet bağlı kalır.

EKRANSIZ MINI PC (oturum açma ekranı)
  Pencerede Genel > "Oturum açılmadan önce başlat" (bir kez yönetici izni ister). EKRAN Windows hizmeti olarak
  C:\Program Files\EKRAN'a kurulur ve açılışta başlar; kilit ve şifre ekranı tablette görünür.
  Kilit perdesini tablette yukarı kaydırın. Windows kilit ekranını yalnızca ana monitörde gösterir.

SORUN OLURSA
  Pencerede Genel > "Günlüğü aç" (%LOCALAPPDATA%\EKRAN\ekran.log)
  Ayrıntılı ölçümler için konsol sürümü: ekran-host.exe

Kaynak kodu ve lisans: GPL-3.0 (LICENSE). adb, Android SDK platform-tools'tandır (adb\NOTICE.txt).
"@ | Set-Content (Join-Path $hedef 'BENIOKU.txt') -Encoding utf8

$zip = Join-Path $dist "$ad.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $hedef -DestinationPath $zip
'{0}  ({1:N1} MB)' -f $zip, ((Get-Item $zip).Length / 1MB)

$apkHedef = Join-Path $dist "EKRAN-$versionName-android.apk"
Copy-Item -Force $apk $apkHedef
'{0}  ({1:N1} MB)' -f $apkHedef, ((Get-Item $apkHedef).Length / 1MB)
