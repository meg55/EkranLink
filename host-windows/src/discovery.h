// Wi-Fi / USB internet paylaşımı ağında EKRAN tabletlerini bulma (UDP).
// Soru her ağ arayüzünden (o arayüze bağlı ayrı soketle) kendi yayın adresine gönderilir; böylece aynı
// ağa iki kartla (Ethernet + Wi-Fi) bağlı PC'de de, USB internet paylaşımıyla açılan ağda da (ör.
// 192.168.42.x) soru her yere çıkar. Wi-Fi yayını onaysızdır ve güç tasarrufundaki tablet kaçırabilir:
// soru süre içinde birkaç kez yinelenir, bilinen tablet adreslerine ayrıca doğrudan sorulur.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ekran {

struct FoundDevice {
    std::string ip;
    uint16_t port = 0;
    std::string name;
    std::string id;
};

// known: daha önce bağlanılan tablet adresleri (doğrudan sorulur)
std::vector<FoundDevice> discoverDevices(int timeoutMs, const std::vector<std::string>& known = {});

}  // namespace ekran
