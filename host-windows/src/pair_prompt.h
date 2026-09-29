// Wi-Fi eşleştirme sorusu (PC tarafı). Tepsi ve hizmet ajanı ortak kullanır.
#pragma once

#include <windows.h>

#include <string>

namespace ekran {

std::wstring widen(const std::string& s);

// "Tablette görünen kod bu mu?" Engelleyicidir; tablet reddederse (cancel) pencere kendiliğinden kapanır.
// Pencere ayrı bir iş parçacığında açılır: çağıran iş parçacığının penceresi olmaz (masaüstü değiştirebilir).
bool askPairing(const std::wstring& device, const std::wstring& code, HANDLE cancel);

}  // namespace ekran
