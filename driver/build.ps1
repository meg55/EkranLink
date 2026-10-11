<#
.SYNOPSIS
    EkranLink sanal ekran sürücüsünü derler: driver\out\ (EkranLinkVD.dll, ekranlinkvd.inf, ekranlinkvd.cat)

.DESCRIPTION
    Gerekenler: Visual Studio Build Tools (MSVC, x64) + Windows Driver Kit (WDK 10.0.26100).
    MSBuild'in sürücü araç setine gerek yok; cl/link doğrudan çağrılır.
    İmzalama ve kurulum ayrı: .\kur.ps1 (yönetici).
#>
param(
    [string] $Surum = '1.0.0.0'
)

$ErrorActionPreference = 'Stop'
$kok = $PSScriptRoot
$src = Join-Path $kok 'src'
$out = Join-Path $kok 'out'
$obj = Join-Path $kok 'obj'
New-Item -ItemType Directory -Force $out, $obj | Out-Null

# --- MSVC ortamı (vcvars64) ---
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC (x64) bulunamadı. Visual Studio Build Tools "C++ ile masaüstü geliştirme" gerekli.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
}

# --- WDK ---
$wdk = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdk = '10.0.26100.0'
$umdf = '2.25'      # Windows 10 1903+
$iddcx = '1.10'     # derleme sürümü; çalışma zamanında en az 1.4 (Windows 10 2004) aranır
foreach ($p in "Include\wdf\umdf\$umdf\wdf.h", "Include\$sdk\um\iddcx\$iddcx\IddCx.h", "Lib\wdf\umdf\x64\$umdf\WdfDriverStubUm.lib") {
    if (-not (Test-Path (Join-Path $wdk $p))) { throw "WDK dosyası yok: $p" }
}

$defs = @('UNICODE', '_UNICODE', 'UMDF_DRIVER', 'IDDCX_VERSION_MAJOR=1', 'IDDCX_VERSION_MINOR=10',
          'IDDCX_MINIMUM_VERSION_REQUIRED=4', 'UMDF_VERSION_MAJOR=2', 'UMDF_VERSION_MINOR=25',
          '_WIN64', '_AMD64_', 'AMD64', '_WIN32_WINNT=0x0A00', 'WINVER=0x0A00', 'NTDDI_VERSION=0x0A000007') |
    ForEach-Object { "/D$_" }
$incs = @($src, "$wdk\Include\wdf\umdf\$umdf", "$wdk\Include\$sdk\um\iddcx\$iddcx") | ForEach-Object { "/I$_" }

Write-Host 'Derleniyor...' -ForegroundColor Cyan
& cl.exe /nologo /c /O2 /MT /EHsc /std:c++17 /W3 /GS /Gy /Zc:wchar_t /permissive- /utf-8 /wd4005 @defs @incs `
    (Join-Path $src 'Driver.cpp') "/Fo$obj\"
if ($LASTEXITCODE -ne 0) { throw 'cl başarısız' }

$libs = @("/LIBPATH:$wdk\Lib\wdf\umdf\x64\$umdf", "/LIBPATH:$wdk\Lib\$sdk\um\x64\iddcx\$iddcx", "/LIBPATH:$wdk\Lib\$sdk\um\x64")
& link.exe /nologo /DLL /MACHINE:X64 /SUBSYSTEM:WINDOWS /NXCOMPAT /DYNAMICBASE /OPT:REF /OPT:ICF `
    "/OUT:$out\EkranLinkVD.dll" (Join-Path $obj 'Driver.obj') @libs `
    WdfDriverStubUm.lib IddCxStub.lib OneCoreUAP.lib avrt.lib d3d11.lib dxgi.lib ntdll.lib
if ($LASTEXITCODE -ne 0) { throw 'link başarısız' }

# --- INF: sürüm, mimari ve UMDF sürümü damgası ---
Copy-Item (Join-Path $src 'ekranlinkvd.inf') $out -Force
$stampinf = Join-Path $wdk "bin\$sdk\x64\stampinf.exe"
& $stampinf -f (Join-Path $out 'ekranlinkvd.inf') -a amd64 -d * -v $Surum -u "$umdf.0" | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'stampinf başarısız' }

Get-ChildItem $out | Select-Object Name, Length | Format-Table -AutoSize
