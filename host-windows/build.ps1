# Windows sunucusunu derler. Visual Studio gerekmez: MinGW-w64 (GCC) + CMake + Ninja yeterli.
#   winget install BrechtSanders.WinLibs.POSIX.MSVCRT
param([switch] $Temiz)

$ErrorActionPreference = 'Stop'
$kaynak = $PSScriptRoot
$cikti = Join-Path $kaynak 'build'

foreach ($arac in 'g++', 'cmake', 'ninja') {
    if (-not (Get-Command $arac -ErrorAction SilentlyContinue)) {
        throw "$arac bulunamadı. Kurulum: winget install BrechtSanders.WinLibs.POSIX.MSVCRT"
    }
}
if ($Temiz -and (Test-Path $cikti)) { Remove-Item -Recurse -Force $cikti }

cmake -S $kaynak -B $cikti -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release | Out-Null
cmake --build $cikti
if ($LASTEXITCODE -eq 0) {
    foreach ($n in 'ekran.exe', 'ekran-host.exe', 'ekran-surucu.exe') {
        $exe = Join-Path $cikti $n
        '{0}  ({1:N0} KB)' -f $exe, ((Get-Item $exe).Length / 1KB)
    }
}
