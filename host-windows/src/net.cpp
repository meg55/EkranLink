#include "net.h"

#include <ws2tcpip.h>

#include <cstdio>
#include <atomic>
#include <cstring>
#include <thread>

#include "log.h"
#include "pairing.h"

namespace ekran {

bool Connection::connect(const std::string& host, uint16_t port, int timeoutMs) {
    close();
    broken_ = false;
    tx_.reset();
    rx_.reset();
    txSeq_ = rxSeq_ = 0;
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[8];
    std::snprintf(portStr, sizeof portStr, "%u", port);
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) return false;

    sock_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock_ == INVALID_SOCKET) {
        freeaddrinfo(res);
        return false;
    }
    // Zaman aşımlı bağlan: engellemesiz moda al, select ile bekle.
    u_long nb = 1;
    ioctlsocket(sock_, FIONBIO, &nb);
    ::connect(sock_, res->ai_addr, int(res->ai_addrlen));
    freeaddrinfo(res);
    fd_set w, e;
    FD_ZERO(&w);
    FD_ZERO(&e);
    FD_SET(sock_, &w);
    FD_SET(sock_, &e);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (select(0, nullptr, &w, &e, &tv) <= 0 || FD_ISSET(sock_, &e)) {
        close();
        return false;
    }
    nb = 0;
    ioctlsocket(sock_, FIONBIO, &nb);

    BOOL on = TRUE;
    setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
    return true;
}

bool Connection::handshake(bool trustedLink, const PairingHooks& hooks, proto::Hello& hello) {
    // adb forward bağlantıyı tablet dinlemese de kabul eder; cevap gelmezse boşuna beklemeyelim.
    DWORD timeout = 3000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);

    proto::Writer magic;
    magic.bytes(proto::kMagic, 4);
    magic.u16(proto::kVersion);
    WSABUF b{ULONG(magic.buf.size()), reinterpret_cast<char*>(magic.buf.data())};
    if (!sendAll(&b, 1)) return false;

    // 1) PC → tablet: kimlik + geçici ECDH anahtarı
    crypto::Ecdh ecdh;
    if (!ecdh.generate()) return false;
    const auto pcId = pairing::pcId();
    uint8_t pcNonce[16];
    crypto::random(pcNonce, 16);
    const auto pcPub = ecdh.publicKey();
    {
        proto::Writer w;
        w.u8(trustedLink ? proto::kWantPlain : 0);
        w.bytes(pcId.data(), 16);
        w.bytes(pcPub.data(), 65);
        w.bytes(pcNonce, 16);
        const std::string name = pairing::pcName();
        w.u16(uint16_t(name.size()));
        w.bytes(name.data(), name.size());
        if (!send(proto::kPcHello, w.buf)) return false;
    }

    // 2) Tablet → PC: geçici anahtar + kimlik + "bu PC'yi tanıyorum" bilgisi
    uint8_t type = 0;
    std::vector<uint8_t> payload;
    if (!readMessage(type, payload) || type != proto::kTabletKey) return false;
    proto::Reader r(payload.data(), payload.size());
    uint8_t tPub[65], tNonce[16];
    for (auto& x : tPub) x = r.u8();
    for (auto& x : tNonce) x = r.u8();
    deviceId_ = r.str();
    const std::string deviceName = r.str();
    const bool tabletKnowsUs = r.u8() != 0;
    if (!r.ok() || deviceId_.empty()) return false;

    // 3) Ortak sır ve oturum anahtarları
    crypto::Bytes32 z{};
    if (!ecdh.agree(tPub, z)) {
        logf("Güvenlik: tabletin anahtarı geçersiz");
        return false;
    }
    std::vector<uint8_t> transcript;
    transcript.insert(transcript.end(), pcPub.begin(), pcPub.end());
    transcript.insert(transcript.end(), tPub, tPub + 65);
    transcript.insert(transcript.end(), pcNonce, pcNonce + 16);
    transcript.insert(transcript.end(), tNonce, tNonce + 16);
    transcript.insert(transcript.end(), pcId.begin(), pcId.end());
    transcript.insert(transcript.end(), deviceId_.begin(), deviceId_.end());
    const crypto::Bytes32 h = crypto::sha256(transcript.data(), transcript.size());

    crypto::Bytes32 pairKey{};
    const bool paired = tabletKnowsUs && pairing::load(deviceId_, pairKey);
    uint8_t ikm[64];
    std::memcpy(ikm, z.data(), 32);
    std::memcpy(ikm + 32, pairKey.data(), 32);  // eşleşmemişse sıfırlar
    const crypto::Bytes32 prk = crypto::hmac(h.data(), 32, ikm, 64);
    SecureZeroMemory(ikm, sizeof ikm);

    const bool encrypt = !trustedLink;
    {
        proto::Writer w;
        w.u8(paired ? 1 : 0);
        w.u8(encrypt ? 1 : 0);
        if (!send(proto::kMode, w.buf)) return false;
    }
    if (encrypt) {
        tx_ = std::make_unique<crypto::DirectionKey>();
        rx_ = std::make_unique<crypto::DirectionKey>();
        if (!tx_->init(crypto::derive(prk, "ekran p2t key")) || !rx_->init(crypto::derive(prk, "ekran t2p key")))
            return false;
    }

    // 4) İlk eşleştirme: USB'de otomatik; Wi-Fi'de iki tarafta da aynı kod onaylanır.
    if (!paired) {
        const crypto::Bytes32 sas = crypto::derive(prk, "ekran sas");
        const uint32_t n = ((uint32_t(sas[0]) << 24) | (uint32_t(sas[1]) << 16) | (uint32_t(sas[2]) << 8) | sas[3]) % 1000000;
        char code[8];
        std::snprintf(code, sizeof code, "%03u %03u", n / 1000, n % 1000);

        bool accepted = trustedLink;
        if (!trustedLink) {
            timeout = 0;  // kullanıcıyı bekliyoruz
            setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
            logf("Eşleştirme isteği: %s, kod %s", deviceName.c_str(), code);
            // Soru açıkken bağlantıyı izle: tablet "Reddet" derse bağlantıyı kapatır → soru kapanır.
            HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::atomic<bool> asking{true};
            std::thread watcher([&] {
                while (asking) {
                    fd_set rs;
                    FD_ZERO(&rs);
                    FD_SET(sock_, &rs);
                    timeval tv{0, 200000};
                    if (select(0, &rs, nullptr, nullptr, &tv) > 0) {  // veri ya da kapanma
                        char c;
                        if (recv(sock_, &c, 1, MSG_PEEK) <= 0) {
                            SetEvent(cancel);
                            return;
                        }
                        Sleep(200);
                    }
                }
            });
            accepted = hooks.confirm && hooks.confirm(deviceName, code, cancel) &&
                       WaitForSingleObject(cancel, 0) != WAIT_OBJECT_0;
            asking = false;
            watcher.join();
            CloseHandle(cancel);
        }
        proto::Writer w;
        w.u8(accepted ? 1 : 0);
        // Tablette "Reddet" bağlantıyı kapatır: gönderim/okuma hatası da ret sayılır.
        const bool tabletAccepted = send(proto::kPairResult, w.buf) && readMessage(type, payload) &&
                                    type == proto::kPairResult && payload.size() == 1 && payload[0] == 1;
        if (!accepted || !tabletAccepted) {
            logf("Eşleştirme reddedildi (%s)", deviceName.c_str());
            pairingRejected_ = true;
            return false;
        }
        pairing::save(deviceId_, crypto::derive(prk, "ekran pair"));
        logf("Eşleştirildi: %s", deviceName.c_str());
    }

    // 5) Tabletin Hello mesajı (Wi-Fi'de şifreli; anahtar uyuşmazsa doğrulama burada düşer)
    timeout = 5000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    const bool ok = readMessage(type, payload) && type == proto::kHello;
    timeout = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    if (!ok) {
        if (paired && encrypt) {
            // Tablet eşleşmeyi unutmuş/değiştirmiş olabilir: bizim kaydı sil, bir dahaki sefere yeniden eşleş.
            logf("Güvenlik: %s doğrulanamadı; eşleşme silindi, yeniden eşleştirilecek", deviceName.c_str());
            pairing::forget(deviceId_);
        }
        return false;
    }

    proto::Reader hr(payload.data(), payload.size());
    hello.version = hr.u16();
    hello.surfaceW = hr.u16();
    hello.surfaceH = hr.u16();
    hello.screenW = hr.u16();
    hello.screenH = hr.u16();
    hello.dpi = hr.u16();
    hello.sdk = hr.u16();
    hello.model = hr.str();
    hello.decoder = hr.str();
    return hr.ok();
}

bool Connection::send(uint8_t type, const std::vector<uint8_t>& payload) {
    return sendFrame(type, nullptr, 0, payload.data(), payload.size());
}

bool Connection::sendVideo(int64_t ptsUs, uint8_t flags, const uint8_t* data, size_t size) {
    proto::Writer h;
    h.u64(uint64_t(ptsUs));
    h.u8(flags);
    return sendFrame(proto::kVideo, h.buf.data(), h.buf.size(), data, size);
}

bool Connection::sendFrame(uint8_t type, const uint8_t* head, size_t headLen, const uint8_t* data, size_t dataLen) {
    const size_t bodyLen = 1 + headLen + dataLen;
    uint8_t len[4] = {uint8_t(bodyLen >> 24), uint8_t(bodyLen >> 16), uint8_t(bodyLen >> 8), uint8_t(bodyLen)};

    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!tx_) {
        // Şifresiz (USB): kopya yapmadan gönder
        WSABUF bufs[4] = {{4, reinterpret_cast<char*>(len)},
                          {1, reinterpret_cast<char*>(&type)},
                          {ULONG(headLen), reinterpret_cast<char*>(const_cast<uint8_t*>(head))},
                          {ULONG(dataLen), reinterpret_cast<char*>(const_cast<uint8_t*>(data))}};
        return sendAll(bufs, 4);
    }
    txBuf_.resize(bodyLen + 16);
    txBuf_[0] = type;
    if (headLen) std::memcpy(&txBuf_[1], head, headLen);
    if (dataLen) std::memcpy(&txBuf_[1 + headLen], data, dataLen);
    tx_->encrypt(txSeq_++, txBuf_.data(), bodyLen, &txBuf_[bodyLen]);
    WSABUF bufs[2] = {{4, reinterpret_cast<char*>(len)},
                      {ULONG(txBuf_.size()), reinterpret_cast<char*>(txBuf_.data())}};
    return sendAll(bufs, 2);
}

bool Connection::readMessage(uint8_t& type, std::vector<uint8_t>& payload) {
    uint8_t len[4];
    if (!recvAll(len, 4)) return false;
    const uint32_t bodyLen = (uint32_t(len[0]) << 24) | (uint32_t(len[1]) << 16) | (uint32_t(len[2]) << 8) | len[3];
    if (bodyLen < 1 || bodyLen > (1u << 24)) {  // bozuk akışa karşı
        broken_ = true;
        return false;
    }
    const size_t total = bodyLen + (rx_ ? 16 : 0);
    payload.resize(total);
    if (!recvAll(payload.data(), total)) return false;
    if (rx_ && !rx_->decrypt(rxSeq_++, payload.data(), bodyLen, payload.data() + bodyLen)) {
        logf("Güvenlik: mesaj doğrulanamadı, bağlantı kesiliyor");
        broken_ = true;
        return false;
    }
    type = payload[0];
    payload.erase(payload.begin());
    payload.resize(bodyLen - 1);
    return true;
}

bool Connection::sendAll(WSABUF* bufs, DWORD count) {
    if (sock_ == INVALID_SOCKET || broken_) return false;
    while (count > 0) {
        DWORD sent = 0;
        if (WSASend(sock_, bufs, count, &sent, 0, nullptr, nullptr) != 0) {
            broken_ = true;
            return false;
        }
        while (count > 0 && sent >= bufs->len) {
            sent -= bufs->len;
            bufs++;
            count--;
        }
        if (count > 0) {
            bufs->buf += sent;
            bufs->len -= sent;
        }
    }
    return true;
}

bool Connection::recvAll(void* p, size_t n) {
    auto* b = static_cast<char*>(p);
    while (n > 0) {
        int r = recv(sock_, b, int(n), 0);
        if (r <= 0) {
            broken_ = true;
            return false;
        }
        b += r;
        n -= size_t(r);
    }
    return true;
}

void Connection::close() {
    if (sock_ != INVALID_SOCKET) {
        shutdown(sock_, SD_BOTH);
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
}

}  // namespace ekran
