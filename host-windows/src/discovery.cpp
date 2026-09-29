#include "discovery.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <cstring>

#include "com.h"
#include "protocol.h"

namespace ekran {

namespace {

std::vector<in_addr> broadcastAddresses() {
    std::vector<in_addr> out;
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
            const uint32_t mask = prefix >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefix)) - 1);
            in_addr b;
            b.s_addr = htonl(ip | ~mask);
            out.push_back(b);
        }
    }
    return out;
}

}  // namespace

std::vector<FoundDevice> discoverDevices(int timeoutMs) {
    std::vector<FoundDevice> found;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return found;
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);

    proto::Writer q;
    q.bytes("EKRN?", 5);
    q.u16(proto::kVersion);
    for (const in_addr& b : broadcastAddresses()) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(proto::kPort);
        to.sin_addr = b;
        sendto(s, reinterpret_cast<const char*>(q.buf.data()), int(q.buf.size()), 0,
               reinterpret_cast<sockaddr*>(&to), sizeof to);
    }

    const int64_t deadline = nowUs() + int64_t(timeoutMs) * 1000;
    while (true) {
        const int64_t left = deadline - nowUs();
        if (left <= 0) break;
        fd_set r;
        FD_ZERO(&r);
        FD_SET(s, &r);
        timeval tv{long(left / 1000000), long(left % 1000000)};
        if (select(0, &r, nullptr, nullptr, &tv) <= 0) break;

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
    }
    closesocket(s);
    return found;
}

}  // namespace ekran
