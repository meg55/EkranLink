cmake_minimum_required(VERSION 3.20)
project(ekranlink_host CXX RC)

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
  src/virtual_display.cpp
  src/display_power.cpp
  src/usb_watch.cpp
  src/desktop.cpp
  src/config.cpp
  src/i18n.cpp
  src/pair_prompt.cpp
  src/streamer.cpp
)

# Windows 10 1809+ API'leri
target_compile_definitions(ekran_core PUBLIC
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
  _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006)
target_link_libraries(ekran_core PUBLIC
  d3d11 dxgi mfplat mfuuid mf ole32 oleaut32 ws2_32 iphlpapi user32 bcrypt advapi32 setupapi psapi shell32 cfgmgr32)

# ekranlink-host.exe: konsol (geliştirme / tanı)
add_executable(ekranlink-host src/main.cpp res/app.rc)
target_link_libraries(ekranlink-host PRIVATE ekran_core)

# EkranLink.exe: bildirim alanı uygulaması (günlük kullanım)
# Simge ve sürüm bilgisi: res/ (simgeler tools/simge.py ile üretilir)
add_executable(EkranLink WIN32 src/tray.cpp src/ui.cpp src/service.cpp res/app.rc)
target_link_libraries(EkranLink PRIVATE ekran_core shell32 advapi32 gdi32 d2d1 dwrite dwmapi)

# ekranlink-surucu.exe: sanal ekran sürücüsü aygıtını kur/kaldır (yönetici)
add_executable(ekranlink-surucu src/driver_tool.cpp res/app.rc)
target_compile_definitions(ekranlink-surucu PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)

# Sürüm bilgisi version.h'den: CMake .rc dosyasının #include'larını izlemez, sürüm değişince yeniden derlensin
# (yoksa dosya özelliklerinde eski sürüm kalır; kod imzalamada ürün sürümü tüm dosyalarda aynı olmalı)
set_source_files_properties(res/app.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h)
target_link_libraries(ekranlink-surucu PRIVATE setupapi newdev cfgmgr32)

# ekranlink-test.exe: şifreleme test vektörleri
add_executable(ekranlink-test tests/crypto_test.cpp)
target_link_libraries(ekranlink-test PRIVATE ekran_core)

if(MINGW)
  target_compile_options(ekran_core PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  target_link_options(EkranLink PRIVATE -municode)
  target_link_options(ekranlink-surucu PRIVATE -municode)
  # Tek dosya: MinGW çalışma zamanı DLL'lerine bağımlılık yok
  foreach(t ekranlink-host EkranLink ekranlink-test ekranlink-surucu)
    target_link_options(${t} PRIVATE -static -s)
  endforeach()
endif()
