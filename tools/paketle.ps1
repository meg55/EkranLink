<#
.SYNOPSIS
    Taşınabilir Windows paketi üretir: dist\EKRAN-<sürüm>-windows.zip

.DESCRIPTION
    İçerik: ekran.exe (bildirim alanı), ekran-host.exe (konsol), ekran-android.apk (+ sürüm dosyası),
    adb\ (Android SDK platform-tools'tan), BENIOKU.txt. Kurulum gerektirmez; zip'i açıp ekran.exe'yi çalıştırın.

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
Copy-Item $apk (Join-Path $hedef 'ekran-android.apk')
Set-Content (Join-Path $hedef 'ekran-android.version') $versionCode -Encoding ascii -NoNewline
foreach ($f in 'adb.exe', 'AdbWinApi.dll', 'AdbWinUsbApi.dll', 'NOTICE.txt') {
    Copy-Item (Join-Path $pt $f) (Join-Path $hedef 'adb')
}
Copy-Item (Join-Path $kok 'LICENSE') $hedef

@"
EKRAN $versionName — eski tabletleri bilgisayar ekranı yapın
===========================================================

Kurulum gerekmez. ekran.exe'yi çalıştırın; saatin yanında bir monitör simgesi belirir.

TABLETE UYGULAMAYI KURMAK
  USB:   Tablette Geliştirici seçenekleri > USB hata ayıklama açıksa tableti takmanız yeterli;
         EKRAN uygulamayı kendisi kurar ve açar.
  Wi-Fi: Simgeye tıklayın > "Tablete uygulamayı kur…", çıkan adresi tabletin tarayıcısında açın.

KULLANIM
  Tablette EKRAN açıkken bilgisayar tableti kendiliğinden bulur (önce USB, yoksa Wi-Fi).
  Wi-Fi ile ilk bağlantıda iki cihazda aynı 6 haneli kod görünür; ikisinde de onaylayın.
  Tablet Windows'a çoklu dokunmatik ekran olarak bağlanır. Üç parmakla dokunmak ölçümleri gösterir.

GÖRÜNTÜ MODLARI (simge > Görüntü)
  İkinci ekran  Tablet ayrı bir monitör olur. Tableti dik çevirmek monitörü de dikey yapar
                (tablette "Otomatik döndür" açık olmalı).
  Tek ekran     Tablet bağlıyken diğer monitörler kapanır; tablet ayrılınca geri gelir.
  Yansıtma      Seçili PC ekranının kopyası.
  İkinci ekran ve tek ekran için EKRAN sanal ekran sürücüsü gerekir. Şimdilik yalnızca kaynak koddan
  derlenip kurulabiliyor (driver\README.md); sürücü yoksa EKRAN yansıtır.
  Windows ekranı kapatınca tablet de kapanır, açınca birlikte uyanır.

SORUN OLURSA
  Simge > "Günlüğü aç" (%LOCALAPPDATA%\EKRAN\ekran.log)
  Ayrıntılı ölçümler için konsol sürümü: ekran-host.exe

Kaynak kodu ve lisans: GPL-3.0 (LICENSE). adb, Android SDK platform-tools'tandır (adb\NOTICE.txt).
"@ | Set-Content (Join-Path $hedef 'BENIOKU.txt') -Encoding utf8

$zip = Join-Path $dist "$ad.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $hedef -DestinationPath $zip
'{0}  ({1:N1} MB)' -f $zip, ((Get-Item $zip).Length / 1MB)
