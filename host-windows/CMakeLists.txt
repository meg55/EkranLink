cmake_minimum_required(VERSION 3.20)
project(ekran_host CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Ortak çekirdek: yakalama, ölçekleme, kodlama, ağ, giriş, bulma
add_library(ekran_core STATIC
  src/log.cpp
  src/crypto.cpp
  src/chacha.cpp
  src/pairing.cpp
  src/capture.cpp
  src/scaler.cpp
  src/encoder.cpp
  src/net.cpp
  src/input.cpp
  src/adb.cpp
  src/discovery.cpp
  src/apkserver.cpp
  src/virtual_display.cpp
  src/display_power.cpp
  src/desktop.cpp
  src/config.cpp
  src/pair_prompt.cpp
  src/streamer.cpp
)

# Windows 10 1809+ API'leri
target_compile_definitions(ekran_core PUBLIC
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
  _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006)
target_link_libraries(ekran_core PUBLIC
  d3d11 dxgi mfplat mfuuid mf ole32 oleaut32 ws2_32 iphlpapi user32 bcrypt advapi32 setupapi psapi shell32)

# ekran-host.exe: konsol (geliştirme / tanı)
add_executable(ekran-host src/main.cpp)
target_link_libraries(ekran-host PRIVATE ekran_core)

# ekran.exe: bildirim alanı uygulaması (günlük kullanım)
add_executable(ekran WIN32 src/tray.cpp src/service.cpp)
target_link_libraries(ekran PRIVATE ekran_core shell32 advapi32 gdi32)

# ekran-surucu.exe: sanal ekran sürücüsü aygıtını kur/kaldır (yönetici)
add_executable(ekran-surucu src/driver_tool.cpp)
target_compile_definitions(ekran-surucu PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(ekran-surucu PRIVATE setupapi newdev cfgmgr32)

# ekran-test.exe: şifreleme test vektörleri
add_executable(ekran-test tests/crypto_test.cpp)
target_link_libraries(ekran-test PRIVATE ekran_core)

if(MINGW)
  target_compile_options(ekran_core PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  target_link_options(ekran PRIVATE -municode)
  target_link_options(ekran-surucu PRIVATE -municode)
  # Tek dosya: MinGW çalışma zamanı DLL'lerine bağımlılık yok
  foreach(t ekran-host ekran ekran-test ekran-surucu)
    target_link_options(${t} PRIVATE -static -s)
  endforeach()
endif()
