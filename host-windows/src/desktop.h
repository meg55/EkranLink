// Windows'un "giriş masaüstü" takibi. Kilit ekranı, oturum açma ve UAC ayrı bir masaüstünde (Winlogon)
// çizilir; oraya yalnızca SYSTEM olarak çalışan bir süreç erişebilir (hizmetin başlattığı ajan).
// Yakalayan ve dokunma ileten iş parçacıkları o an ekranda olan masaüstüne geçmelidir.
#pragma once

#include <string>

namespace ekran {

// Takibi aç (yalnızca ajan: SYSTEM). Kapalıyken followInputDesktop hiçbir şey yapmaz (tepsi, konsol).
void enableDesktopFollowing(bool on);

// Çağıran iş parçacığını şimdiki giriş masaüstüne bağlar. Masaüstü değiştiyse true.
// Erişim yoksa hiçbir şey yapmaz. İş parçacığının penceresi olmamalı.
bool followInputDesktop();

// Şimdiki giriş masaüstünün adı ("Default", "Winlogon"); erişilemiyorsa boş.
std::wstring inputDesktopName();

}  // namespace ekran
