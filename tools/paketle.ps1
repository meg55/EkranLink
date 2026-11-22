<#
.SYNOPSIS
    İndirmeleri üretir: dist\EkranLink-<sürüm>-windows-setup.exe (kurulum), dist\EkranLink-<sürüm>-windows.zip
    (taşınabilir), dist\EkranLink-<sürüm>-android.apk ve (anahtar varsa) Google Play için
    dist\EkranLink-<sürüm>-android-play.aab

.DESCRIPTION
    Windows paketi: EkranLink.exe (bildirim alanı), ekranlink-host.exe (konsol), adb\ (Android SDK platform-tools'tan),
    README.txt (iki dilli). Zip kurulum gerektirmez; zip'i açıp EkranLink.exe'yi çalıştırın. Kurulum programı Inno Setup 6 ile
    derlenir (host-windows\installer\EkranLink.iss); Inno Setup yoksa atlanır (CI'da hata).
    Tablet uygulaması pakette yok: PC tablete uygulama kurmaz, APK ayrıca indirilip tablete kurulur.

    APK sürüm anahtarıyla imzalanır (v1+v2+v3; Android 4.x için v1 şart). Anahtar depoda değildir; yolu
    ~/.gradle/gradle.properties içindeki releaseKeystoreProperties ya da RELEASE_KEYSTORE_PROPERTIES verir.
#>
param([switch] $Temiz)

$ErrorActionPreference = 'Stop'
$kok = Split-Path -Parent $PSScriptRoot

# --- Sürüm (android/app/build.gradle) ---
$gradle = Get-Content (Join-Path $kok 'android\app\build.gradle') -Raw
$versionCode = [regex]::Match($gradle, 'versionCode\s+(\d+)').Groups[1].Value
$versionName = [regex]::Match($gradle, "versionName\s+'([^']+)'").Groups[1].Value
Write-Host "EkranLink $versionName (Android versionCode $versionCode)" -ForegroundColor Cyan
$kVersion = [regex]::Match((Get-Content (Join-Path $kok 'host-windows\src\version.h') -Raw), '#define EKRANLINK_VERSION "([^"]+)"').Groups[1].Value
if ($kVersion -ne $versionName) { throw "Sürümler farklı: build.gradle $versionName, version.h $kVersion" }

# --- Android ---
if (-not $env:JAVA_HOME) {
    $jbr = 'C:\Program Files\Android\Android Studio\jbr'
    if (Test-Path $jbr) { $env:JAVA_HOME = $jbr }
}
Push-Location (Join-Path $kok 'android')
try {
    # --no-daemon: arkada kalan Gradle sunucusu yüzlerce MB bellek tutar ve çıktı borusunu açık bırakır
    # Sürüm anahtarı tanımlıysa (android/app/build.gradle: releaseKeystoreProperties / RELEASE_KEYSTORE_PROPERTIES)
    # imzalı sürüm, değilse geliştirme anahtarıyla imzalı hata ayıklama derlemesi
    # bundleRelease: Google Play'e yüklenen .aab (GitHub sürümüne konmaz)
    & .\gradlew.bat assembleRelease bundleRelease assembleDebug --no-daemon --console=plain -q
    if ($LASTEXITCODE -ne 0) { throw 'Android derlemesi başarısız' }
} finally { Pop-Location }
$apk = Join-Path $kok 'android\app\build\outputs\apk\release\app-release.apk'
if (-not (Test-Path $apk)) {
    Write-Warning 'Sürüm anahtarı yok: APK geliştirme anahtarıyla imzalı (yayınlanmamalı)'
    $apk = Join-Path $kok 'android\app\build\outputs\apk\debug\app-debug.apk'
}

# --- Windows ---
$buildArgs = @{}
if ($Temiz) { $buildArgs.Temiz = $true }
& (Join-Path $kok 'host-windows\build.ps1') @buildArgs | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Windows derlemesi başarısız' }
$hostBuild = Join-Path $kok 'host-windows\build'
& (Join-Path $hostBuild 'ekranlink-test.exe') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Şifreleme testleri başarısız (ekranlink-test.exe)' }

# --- adb ---
$pt = $null
foreach ($aday in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, (Join-Path $env:LOCALAPPDATA 'Android\Sdk'))) {
    if ($aday -and (Test-Path (Join-Path $aday 'platform-tools\adb.exe'))) { $pt = Join-Path $aday 'platform-tools'; break }
}
if (-not $pt) { throw 'platform-tools bulunamadı (adb pakete eklenemiyor)' }

# --- Paket ---
$dist = Join-Path $kok 'dist'
$ad = "EkranLink-$versionName-windows"
$hedef = Join-Path $dist $ad
if (Test-Path $hedef) { Remove-Item -Recurse -Force $hedef }
New-Item -ItemType Directory -Force (Join-Path $hedef 'adb') | Out-Null

Copy-Item (Join-Path $hostBuild 'EkranLink.exe'), (Join-Path $hostBuild 'ekranlink-host.exe') $hedef
foreach ($f in 'adb.exe', 'AdbWinApi.dll', 'AdbWinUsbApi.dll', 'NOTICE.txt') {
    Copy-Item (Join-Path $pt $f) (Join-Path $hedef 'adb')
}
Copy-Item (Join-Path $kok 'LICENSE') $hedef

@"
EkranLink $versionName - your Android tablet or phone as a PC display
=====================================================================
English first, Türkçesi aşağıda.

ENGLISH

No installation needed. Run EkranLink.exe: the settings window opens and the EkranLink icon appears in the
notification area (on Windows 11 it may be under the ^ arrow). Closing the window does not quit EkranLink;
click the icon to open it again (right-click: menu, Exit). Language: General > Language.

DEVICE APP (separate download)
  Install EkranLink-$versionName-android.apk on the tablet or phone: https://github.com/meg55/EkranLink/releases
  Android asks to allow installing apps from your browser or file manager; allow it.
  For USB: on the device turn on Developer options > USB debugging (no Developer options? tap
  Settings > About phone/tablet > Build number 7 times), then tap Allow on "Allow USB debugging?".

CONNECTING
  Open EkranLink on the device. Over USB the PC finds it by itself. Over Wi-Fi (same network) the device
  appears in the window: click Connect. The first time, confirm the same 6-digit code on both screens.
  Three-finger tap on the device: measurements and Disconnect.

DISPLAY MODES (Display page)
  Mirror works now. Extend and Only this need the EkranLink virtual display driver, which ships once it is
  code-signed; until then EkranLink mirrors.
  Turning the device upright turns the Windows monitor portrait when the device's Auto-rotate is on.
  Settings apply without dropping the connection.

HEADLESS MINI PC
  General > "Start before sign-in" (asks for administrator permission once): the lock and sign-in screens
  appear on the device too.

TROUBLESHOOTING
  General > "Open log" (%LOCALAPPDATA%\EkranLink\ekranlink.log). Console version with measurements:
  ekranlink-host.exe

TÜRKÇE

Kurulum gerekmez. EkranLink.exe'yi çalıştırın: ayar penceresi açılır, EkranLink simgesi bildirim alanında
belirir (Windows 11'de ^ okunun altında olabilir). Pencereyi kapatmak EkranLink'i kapatmaz; simgeye
tıklayınca yeniden açılır (sağ tık: menü, Çıkış). Dil: Genel > Dil.

CİHAZ UYGULAMASI (ayrı indirilir)
  Tablete ya da telefona EkranLink-$versionName-android.apk dosyasını kurun:
  https://github.com/meg55/EkranLink/releases
  Android tarayıcıdan ya da dosya yöneticisinden uygulama yüklemeye izin istemenizi ister; izin verin.
  USB için: cihazda Geliştirici seçenekleri > USB hata ayıklama'yı açın (Geliştirici seçenekleri yoksa
  Ayarlar > Telefon/Tablet hakkında > Yapı numarası'na 7 kez dokunun), sonra "USB hata ayıklamaya izin
  verilsin mi?" sorusunda İzin ver'e dokunun.

BAĞLANMA
  Cihazda EkranLink'i açın. USB'de bilgisayar cihazı kendiliğinden bulur. Wi-Fi'de (aynı ağ) cihaz
  pencerede görünür: Bağlan'a tıklayın. İlk seferde iki ekranda aynı 6 haneli kodu onaylayın.
  Cihazda üç parmakla dokunmak: ölçümler ve Bağlantıyı kes.

GÖRÜNTÜ MODLARI (Görüntü sayfası)
  Yansıtma şimdi çalışır. İkinci ekran ve Tek ekran için EkranLink sanal ekran sürücüsü gerekir; sürücü kod
  imzası tamamlanınca gelecek, o zamana kadar EkranLink yansıtır.
  Cihazda Otomatik döndür açıkken cihazı dik çevirmek Windows'taki monitörü de dikey yapar.
  Ayarlar bağlantıyı koparmadan uygulanır.

EKRANSIZ MİNİ PC
  Genel > "Oturum açılmadan önce başlat" (bir kez yönetici izni ister): kilit ve oturum açma ekranları da
  cihazda görünür.

SORUN OLURSA
  Genel > "Günlüğü aç" (%LOCALAPPDATA%\EkranLink\ekranlink.log). Ölçümlü konsol sürümü: ekranlink-host.exe

License / Lisans: GPL-3.0 (LICENSE). adb comes from the Android SDK platform-tools / adb, Android SDK
platform-tools'tandır (adb\NOTICE.txt).
"@ | Set-Content (Join-Path $hedef 'README.txt') -Encoding utf8

$zip = Join-Path $dist "$ad.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $hedef -DestinationPath $zip
'{0}  ({1:N1} MB)' -f $zip, ((Get-Item $zip).Length / 1MB)

# --- Kurulum programı (Inno Setup 6) ---
$iscc = @((Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
          (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe')) | Where-Object { $_ -and (Test-Path $_) } |
        Select-Object -First 1
if ($iscc) {
    & $iscc /Q "/DSurum=$versionName" "/DKaynak=$hostBuild" "/DAdb=$pt" "/O$dist" (Join-Path $kok 'host-windows\installer\EkranLink.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Kurulum programı derlenemedi (ISCC)' }
    $setup = Join-Path $dist "EkranLink-$versionName-windows-setup.exe"
    '{0}  ({1:N1} MB)' -f $setup, ((Get-Item $setup).Length / 1MB)
} elseif ($env:CI) {
    throw 'Inno Setup 6 bulunamadı (ISCC.exe)'
} else {
    Write-Warning 'Inno Setup 6 yok: kurulum programı atlandı (winget install JRSoftware.InnoSetup)'
}

$apkHedef = Join-Path $dist "EkranLink-$versionName-android.apk"
Copy-Item -Force $apk $apkHedef
'{0}  ({1:N1} MB)' -f $apkHedef, ((Get-Item $apkHedef).Length / 1MB)

# Google Play paketi: yalnızca sürüm anahtarıyla imzalıysa (imzasız .aab Play'e yüklenemez)
if ($apk -like '*app-release.apk') {
    $aabHedef = Join-Path $dist "EkranLink-$versionName-android-play.aab"
    Copy-Item -Force (Join-Path $kok 'android\app\build\outputs\bundle\release\app-release.aab') $aabHedef
    '{0}  ({1:N1} MB, Google Play için)' -f $aabHedef, ((Get-Item $aabHedef).Length / 1MB)
}
