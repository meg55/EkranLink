#include "apkserver.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <cstdio>
#include <cstring>

#include "com.h"
#include "log.h"

namespace ekran {

namespace {

const char* kPage =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>EKRAN'ı kur</title>"
    "<style>body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:24px;line-height:1.5}"
    "h1{font-size:28px}ol{padding-left:22px}"
    "a.b{display:block;margin:24px 0;padding:18px;background:#2a9d5c;color:#fff;text-align:center;"
    "font-size:22px;text-decoration:none;border-radius:6px}small{color:#999}</style></head><body>"
    "<h1>EKRAN'ı tablete kur</h1>"
    "<ol><li><b>Ayarlar &gt; Güvenlik &gt; Bilinmeyen kaynaklar</b> seçeneğini açın "
    "(Android 8 ve sonrasında indirmeyi açarken sorulur).</li>"
    "<li>Aşağıdaki düğmeye dokunun.</li>"
    "<li>İndirme bitince bildirimden <b>ekran.apk</b> dosyasına dokunup kurun.</li></ol>"
    "<a class=\"b\" href=\"/ekran.apk\">EKRAN'ı indir</a>"
    "<small>Bu sayfa bilgisayarınızdaki EKRAN tarafından sunuluyor ve birkaç dakika sonra kapanır.</small>"
    "</body></html>";

bool sendAll(SOCKET s, const char* p, size_t n) {
    while (n > 0) {
        int r = send(s, p, int(n > 65536 ? 65536 : n), 0);
        if (r <= 0) return false;
        p += r;
        n -= size_t(r);
    }
    return true;
}

}  // namespace

std::vector<std::string> localIPv4Addresses() {
    std::vector<std::string> out;
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buf(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    // Ağ geçidi olmayan arayüzler (Hyper-V / WSL / VirtualBox iç ağları) tabletten erişilemez: gösterme.
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
                        GAA_FLAG_INCLUDE_GATEWAYS;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    }
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) != NO_ERROR) return out;
    for (auto* a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || !a->FirstGatewayAddress) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            if ((ntohl(sin->sin_addr.s_addr) >> 16) == 0xA9FE) continue;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof ip);
            out.push_back(ip);
        }
    }
    return out;
}

bool ApkServer::start(const std::wstring& apkPath, uint16_t port, int minutes) {
    deadlineUs_ = nowUs() + int64_t(minutes) * 60'000'000;
    if (running_) return true;
    apk_ = apkPath;
    port_ = port;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(s, 4) != 0) {
        logf("APK sunucusu: %u portu açılamadı", port);
        closesocket(s);
        return false;
    }
    listen_ = uintptr_t(s);
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    logf("APK sunucusu açıldı (%d dk)", minutes);
    return true;
}

void ApkServer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

std::vector<std::string> ApkServer::urls() const {
    std::vector<std::string> out;
    for (const auto& ip : localIPv4Addresses()) {
        char u[64];
        std::snprintf(u, sizeof u, "http://%s:%u", ip.c_str(), port_);
        out.push_back(u);
    }
    return out;
}

void ApkServer::loop() {
    const SOCKET ls = SOCKET(listen_);
    while (running_ && nowUs() < deadlineUs_) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(ls, &r);
        timeval tv{0, 500000};  // durdurma isteğini yarım saniyede fark et
        if (select(0, &r, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        DWORD to = 10000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof to);
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&to), sizeof to);
        serve(uintptr_t(c));
        closesocket(c);
    }
    closesocket(ls);
    running_ = false;
    logf("APK sunucusu kapandı");
}

void ApkServer::serve(uintptr_t client) {
    const SOCKET c = SOCKET(client);
    char req[4096];
    int n = 0;
    while (n < int(sizeof req) - 1) {
        int r = recv(c, req + n, int(sizeof req) - 1 - n, 0);
        if (r <= 0) return;
        n += r;
        req[n] = 0;
        if (std::strstr(req, "\r\n\r\n")) break;
    }
    char method[8] = {}, path[256] = {};
    if (std::sscanf(req, "%7s %255s", method, path) != 2 || std::strcmp(method, "GET") != 0) return;

    char hdr[512];
    if (std::strcmp(path, "/") == 0) {
        const size_t len = std::strlen(kPage);
        std::snprintf(hdr, sizeof hdr,
                      "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\n"
                      "Connection: close\r\n\r\n", len);
        sendAll(c, hdr, std::strlen(hdr)) && sendAll(c, kPage, len);
        return;
    }
    if (std::strcmp(path, "/ekran.apk") == 0) {
        FILE* f = _wfopen(apk_.c_str(), L"rb");
        if (!f) return;
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::snprintf(hdr, sizeof hdr,
                      "HTTP/1.0 200 OK\r\nContent-Type: application/vnd.android.package-archive\r\n"
                      "Content-Disposition: attachment; filename=\"ekran.apk\"\r\nContent-Length: %ld\r\n"
                      "Connection: close\r\n\r\n", size);
        bool ok = sendAll(c, hdr, std::strlen(hdr));
        char chunk[65536];
        size_t r;
        while (ok && (r = std::fread(chunk, 1, sizeof chunk, f)) > 0) ok = sendAll(c, chunk, r);
        std::fclose(f);
        if (ok) logf("APK indirildi (%ld bayt)", size);
        return;
    }
    const char* nf = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    sendAll(c, nf, std::strlen(nf));
}

}  // namespace ekran
