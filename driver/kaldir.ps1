<#
.SYNOPSIS
    EkranLink sanal ekran sürücüsünü tamamen kaldırır: aygıt, sürücü paketi, sertifika, ayarlar. Yönetici olarak çalışır.
#>
param(
    [string] $Arac = (Join-Path $PSScriptRoot '..\host-windows\build\ekranlink-surucu.exe')
)

$ErrorActionPreference = 'Stop'
$konu = 'CN=EkranLink Yerel Surucu Imzasi'

$yonetici = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $yonetici) {
    Write-Host 'Yönetici izni isteniyor...' -ForegroundColor Yellow
    $p = Start-Process pwsh -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Arac `"$Arac`"" -Wait -PassThru
    exit $p.ExitCode
}

# 1) Aygıt
if (Test-Path $Arac) { & $Arac kaldir }

# 2) Sürücü paketi (oemNN.inf)
$paketler = (pnputil /enum-drivers) -join "`n" -split "`n`n" | Where-Object { $_ -match 'ekranlinkvd\.inf' }
foreach ($p in $paketler) {
    if ($p -match '(oem\d+\.inf)') {
        Write-Host "Sürücü paketi siliniyor: $($matches[1])"
        pnputil /delete-driver $matches[1] /uninstall /force | Out-Null
    }
}

# 3) Sertifikalar
foreach ($depo in 'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher', 'Cert:\CurrentUser\My') {
    Get-ChildItem $depo | Where-Object Subject -eq $konu | ForEach-Object {
        Write-Host "Sertifika siliniyor: $depo\$($_.Thumbprint)"
        Remove-Item $_.PSPath -ErrorAction SilentlyContinue
    }
}

# 4) Ayarlar
Remove-Item 'HKLM:\SOFTWARE\EkranLink' -Recurse -ErrorAction SilentlyContinue

Write-Host 'EkranLink sanal ekran sürücüsü kaldırıldı.' -ForegroundColor Green
