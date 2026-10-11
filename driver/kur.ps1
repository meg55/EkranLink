<#
.SYNOPSIS
    EkranLink sanal ekran sürücüsünü bu bilgisayar için imzalar ve kurar. Yönetici olarak çalışır.

.DESCRIPTION
    Kullanıcı modu (UMDF) sürücüsü olduğu için Windows test modu gerekmez; makinenin güvendiği bir
    sertifikayla imzalanması yeterlidir. Güvenlik için:
      1. Her kurulumda YENİ bir kod imzalama sertifikası üretilir (özel anahtar dışa aktarılamaz).
      2. Sürücü (DLL + katalog) bu sertifikayla imzalanır.
      3. Sertifikanın yalnızca açık kısmı "Güvenilen Kök" ve "Güvenilen Yayımcılar" deposuna eklenir.
      4. ÖZEL ANAHTAR HEMEN SİLİNİR: bu sertifikayla başka hiçbir şey imzalanamaz.
      5. Önceki EkranLink sertifikaları temizlenir.
    Kaldırmak için: .\kaldir.ps1
#>
param(
    [string] $Klasor = (Join-Path $PSScriptRoot 'out'),
    [string] $Arac = (Join-Path $PSScriptRoot '..\host-windows\build\ekranlink-surucu.exe')
)

$ErrorActionPreference = 'Stop'
$konu = 'CN=EkranLink Yerel Surucu Imzasi'

$yonetici = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $yonetici) {
    Write-Host 'Yönetici izni isteniyor...' -ForegroundColor Yellow
    $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Klasor `"$Klasor`" -Arac `"$Arac`""
    $p = Start-Process pwsh -Verb RunAs -ArgumentList $arg -Wait -PassThru
    exit $p.ExitCode
}

$gunluk = Join-Path $Klasor 'kur.log'
Start-Transcript -Path $gunluk -Force | Out-Null
try {
    $wdk = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0"
    $signtool = Join-Path $wdk 'x64\signtool.exe'
    $inf2cat = Join-Path $wdk 'x86\Inf2Cat.exe'
    $dll = Join-Path $Klasor 'EkranLinkVD.dll'
    $inf = Join-Path $Klasor 'ekranlinkvd.inf'
    foreach ($f in $signtool, $inf2cat, $dll, $inf, $Arac) { if (-not (Test-Path $f)) { throw "Bulunamadı: $f" } }

    # 5) Eski EkranLink sertifikaları
    foreach ($depo in 'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher', 'Cert:\CurrentUser\My') {
        Get-ChildItem $depo | Where-Object Subject -eq $konu | ForEach-Object {
            Write-Host "Eski sertifika siliniyor: $depo\$($_.Thumbprint)"
            Remove-Item $_.PSPath -DeleteKey -ErrorAction SilentlyContinue
            Remove-Item $_.PSPath -ErrorAction SilentlyContinue
        }
    }

    # 1) Yeni sertifika
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $konu -CertStoreLocation Cert:\CurrentUser\My `
        -KeyExportPolicy NonExportable -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(10)
    Write-Host "Sertifika: $($cert.Thumbprint)"

    try {
        # 2) DLL'i imzala, kataloğu üret ve imzala
        & $signtool sign /q /fd SHA256 /sha1 $cert.Thumbprint /s My $dll
        if ($LASTEXITCODE -ne 0) { throw 'DLL imzalanamadı' }
        Remove-Item (Join-Path $Klasor 'ekranlinkvd.cat') -ErrorAction SilentlyContinue
        & $inf2cat "/driver:$Klasor" /os:10_X64 /uselocaltime | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Katalog (Inf2Cat) üretilemedi' }
        & $signtool sign /q /fd SHA256 /sha1 $cert.Thumbprint /s My (Join-Path $Klasor 'ekranlinkvd.cat')
        if ($LASTEXITCODE -ne 0) { throw 'Katalog imzalanamadı' }
    } finally {
        # 4) Özel anahtar: iş biter bitmez sil
        $cer = Join-Path $env:TEMP "ekran-$($cert.Thumbprint).cer"
        Export-Certificate -Cert $cert -FilePath $cer | Out-Null
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey
        Write-Host 'Özel anahtar silindi.'
    }

    # 3) Açık sertifikaya güven
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
    Remove-Item $cer

    # Sürücü ayarları (HKLM\SOFTWARE\EkranLink\VirtualDisplay): PC programı 3 sn ses vermezse monitörler kaldırılır
    New-Item -Path 'HKLM:\SOFTWARE\EkranLink\VirtualDisplay' -Force | Out-Null
    New-ItemProperty -Path 'HKLM:\SOFTWARE\EkranLink\VirtualDisplay' -Name watchdog -Value 3 -PropertyType DWord -Force | Out-Null

    # Aygıtı oluştur ve sürücüyü kur
    & $Arac kur $inf
    if ($LASTEXITCODE -ne 0) { throw 'Aygıt kurulamadı' }
    & $Arac durum
    Write-Host 'EkranLink sanal ekran sürücüsü kuruldu.' -ForegroundColor Green
    exit 0
} catch {
    Write-Host "HATA: $_" -ForegroundColor Red
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
