#include "discovery.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cstring>

#include "com.h"
#include "protocol.h"

namespace ekran {

namespace {

struct Iface {
    in_addr local;
    in_addr broadcast;
};

std::vector<Iface> interfaces() {
    std::vector<Iface> out;
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buf(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    }
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) != NO_ERROR) return out;

    for (auto* a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            const ULONG prefix = u->OnLinkPrefixLength;
            if (prefix == 0 || prefix > 30) continue;
            const uint32_t ip = ntohl(sin->sin_addr.s_addr);
            if ((ip >> 16) == 0xA9FE) continue;  // 169.254.x.x: DHCP alamamış arayüz
            const uint32_t mask = ~((1u << (32 - prefix)) - 1);
            Iface f;
            f.local = sin->sin_addr;
            f.broadcast.s_addr = htonl(ip | ~mask);
            out.push_back(f);
        }
    }
    return out;
}

void sendTo(SOCKET s, const std::vector<uint8_t>& msg, in_addr addr) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(proto::kPort);
    to.sin_addr = addr;
    sendto(s, reinterpret_cast<const char*>(msg.data()), int(msg.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
}

}  // namespace

std::vector<FoundDevice> discoverDevices(int timeoutMs, const std::vector<std::string>& known) {
    std::vector<FoundDevice> found;
    proto::Writer q;
    q.bytes("EKRN?", 5);
    q.u16(proto::kVersion);

    // Her arayüze bağlı bir soket: yayın o karttan çıkar (bağlanmamış soketle Windows yalnızca yönlendirme
    // tablosunun seçtiği karttan gönderir). Doğrudan sorular bağlanmamış soketten (yönlendirme seçsin).
    std::vector<std::pair<SOCKET, in_addr>> socks;  // soket, yayın adresi (doğrudan soket için INADDR_NONE)
    auto open = [&](const in_addr* local, in_addr broadcast) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return;
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
        if (local) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr = *local;
            if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
                closesocket(s);
                return;
            }
        }
        socks.emplace_back(s, broadcast);
    };
    for (const Iface& f : interfaces()) open(&f.local, f.broadcast);
    std::vector<in_addr> direct;
    for (const std::string& ip : known) {
        in_addr a;
        if (inet_pton(AF_INET, ip.c_str(), &a) == 1) direct.push_back(a);
    }
    in_addr none;
    none.s_addr = INADDR_NONE;
    if (!direct.empty()) open(nullptr, none);
    if (socks.empty()) return found;

    auto ask = [&] {
        for (auto& [s, b] : socks) {
            if (b.s_addr != INADDR_NONE) sendTo(s, q.buf, b);
            else
                for (const in_addr& a : direct) sendTo(s, q.buf, a);
        }
    };

    // Soru süre boyunca 3 kez gönderilir (Wi-Fi yayını kaybolabilir; uyuyan tablet ilk paketle uyanır)
    const int64_t start = nowUs();
    int64_t deadline = start + int64_t(timeoutMs) * 1000;
    const int64_t step = int64_t(timeoutMs) * 1000 / 3;
    int asked = 0;
    while (true) {
        const int64_t now = nowUs();
        if (now >= deadline) break;
        if (asked < 3 && now >= start + asked * step) {
            ask();
            asked++;
        }
        const int64_t until = asked < 3 ? std::min(deadline, start + asked * step) : deadline;
        const int64_t left = std::max<int64_t>(until - nowUs(), 0);
        fd_set r;
        FD_ZERO(&r);
        for (auto& [s, b] : socks) FD_SET(s, &r);
        timeval tv{long(left / 1000000), long(left % 1000000)};
        if (select(0, &r, nullptr, nullptr, &tv) <= 0) continue;

        for (auto& [s, b] : socks) {
            if (!FD_ISSET(s, &r)) continue;
            uint8_t buf[512];
            sockaddr_in from{};
            int fromLen = sizeof from;
            int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n < 9 || std::memcmp(buf, "EKRN!", 5) != 0) continue;
            proto::Reader rd(buf + 5, size_t(n - 5));
            rd.u16();  // sürüm
            FoundDevice d;
            d.port = rd.u16();
            d.name = rd.str();
            d.id = rd.str();
            if (!rd.ok()) continue;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
            d.ip = ip;
            bool dup = false;
            for (auto& e : found) dup |= e.id == d.id;  // aynı tablet birden çok arayüzden cevap verebilir
            if (!dup) found.push_back(std::move(d));
            // Bulundu: başka tablet / arayüz cevapları için kısa bir süre daha bekle, sonra dön
            deadline = std::min(deadline, nowUs() + 150'000);
            asked = 3;
        }
    }
    for (auto& [s, b] : socks) closesocket(s);
    return found;
}

}  // namespace ekran
