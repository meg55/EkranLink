// Wi-Fi / USB internet paylaşımı ağında EKRAN tabletlerini bulma (UDP yayını).
// Soru her ağ arayüzünün kendi yayın adresine gönderilir; böylece USB internet paylaşımıyla
// açılan ağ da (ör. 192.168.42.x) ayrıca bir şey yapmadan taranmış olur.
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

std::vector<FoundDevice> discoverDevices(int timeoutMs);

}  // namespace ekran
