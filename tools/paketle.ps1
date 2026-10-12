<#
.SYNOPSIS
    İndirmeleri üretir: dist\EkranLink-<sürüm>-windows-setup.exe (kurulum), dist\EkranLink-<sürüm>-windows.zip
    (taşınabilir) ve dist\EkranLink-<sürüm>-android.apk

.DESCRIPTION
    Windows paketi: EkranLink.exe (bildirim alanı), ekranlink-host.exe (konsol), adb\ (Android SDK platform-tools'tan),
    BENIOKU.txt. Zip kurulum gerektirmez; zip'i açıp EkranLink.exe'yi çalıştırın. Kurulum programı Inno Setup 6 ile
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
    & .\gradlew.bat assembleRelease assembleDebug --no-daemon --console=plain -q
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
EkranLink $versionName — Android tablet ve telefonunuzu bilgisayar ekranı yapın
===========================================================

Kurulum gerekmez. EkranLink.exe'yi çalıştırın: ayar penceresi açılır (soldaki menü: Durum, Bağlantı,
Görüntü, Dokunma ve performans, Genel), saatin yanında bir monitör simgesi belirir. Pencereyi kapatmak EkranLink'i kapatmaz; simgeye tıklayınca yeniden açılır (sağ tık: menü, çıkış).
Dil: pencerede Genel > Dil (Sistem / Türkçe / English). Tablet ve telefon cihazın dilini kullanır.

TABLET UYGULAMASI (ayrı indirilir)
  Tablete EkranLink-$versionName-android.apk dosyasını kurun: tabletin tarayıcısıyla indirip açın ya da
  USB ile tablete kopyalayıp dosya yöneticisinden açın. Tablet "bilinmeyen kaynaklardan yükleme"
  izni isteyebilir. İki uygulamanın sürümü uyuşmazsa ikisi de hangisinin güncelleneceğini söyler.
  USB ile bağlanmak için tablette Geliştirici seçenekleri > USB hata ayıklama açık olmalı.

KULLANIM
  Tablette EkranLink açıkken bilgisayar tableti kendiliğinden bulur (önce USB, yoksa Wi-Fi).
  Wi-Fi ile ilk bağlantıda iki cihazda aynı 6 haneli kod görünür; ikisinde de onaylayın.
  Tablet Windows'a çoklu dokunmatik ekran olarak bağlanır. Üç parmakla dokunmak ölçümleri gösterir.

GÖRÜNTÜ MODLARI (pencerede Görüntü)
  İkinci ekran  Tablet ayrı bir monitör olur.
  Tek ekran     Tablet bağlıyken diğer monitörler kapanır; tablet ayrılınca geri gelir.
  Yansıtma      Seçili PC ekranının kopyası.
  İkinci ekran ve tek ekran için EkranLink sanal ekran sürücüsü gerekir. Şimdilik yalnızca kaynak koddan
  derlenip kurulabiliyor (driver\README.md); sürücü yoksa EkranLink yansıtır.
  Windows ekranı kapatınca tablet de kapanır, açınca birlikte uyanır.
  Monitör yönü: Otomatik'te tableti dik çevirmek monitörü de dikey yapar (tablette "Otomatik döndür"
  açık olmalı). Yatay / Dikey / ters seçilebilir. Seçim ve Windows ekran ayarlarından yapılan
  değişiklik hatırlanır.
  Ekran çözünürlüğü: Windows Ayarlar > Ekran'daki listenin aynısı; cihazın kendi çözünürlüğü
  "(Önerilen)". Seçim cihaz başına hatırlanır; Windows'tan yapılan değişiklik de.
  Ayarlar bağlantıyı koparmadan uygulanır; tablet bağlı kalır.

EKRANSIZ MINI PC (oturum açma ekranı)
  Pencerede Genel > "Oturum açılmadan önce başlat" (bir kez yönetici izni ister). EkranLink Windows hizmeti olarak
  C:\Program Files\EkranLink'e kurulur ve açılışta başlar; kilit ve şifre ekranı tablette görünür.
  Kilit perdesini tablette yukarı kaydırın. Windows kilit ekranını yalnızca ana monitörde gösterir.

SORUN OLURSA
  Pencerede Genel > "Günlüğü aç" (%LOCALAPPDATA%\EkranLink\ekranlink.log)
  Ayrıntılı ölçümler için konsol sürümü: ekranlink-host.exe

Kaynak kodu ve lisans: GPL-3.0 (LICENSE). adb, Android SDK platform-tools'tandır (adb\NOTICE.txt).
"@ | Set-Content (Join-Path $hedef 'BENIOKU.txt') -Encoding utf8

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
