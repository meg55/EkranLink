// EkranLink sunucusu — konsol sürümü (geliştirme ve tanı için). Günlük kullanım: EkranLink.exe (tepsi).

#include <winsock2.h>
#include <windows.h>
#include <mfapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "log.h"
#include "streamer.h"

using namespace ekran;

namespace {

Streamer* g_streamer = nullptr;

BOOL WINAPI onCtrl(DWORD) {
    if (g_streamer) g_streamer->stop();
    return TRUE;
}

void usage() {
    std::printf(
        "Kullanım: ekranlink-host [seçenekler]\n"
        "  (seçeneksiz)      Otomatik: USB'deki tablet, yoksa Wi-Fi'de aranan tablet\n"
        "  --usb             Sadece USB (adb)\n"
        "  --wifi            Sadece Wi-Fi (ağda otomatik aranır)\n"
        "  --ip ADRES        Wi-Fi ile bu IP'deki tablete bağlan\n"
        "  --seri SERİ       USB'de birden çok cihaz takılıysa yalnızca bu cihaz (adb devices)\n"
        "  --yansit         İkinci ekran yerine mevcut ekranı yansıt (sürücü kurulu değilse zaten yansıtır)\n"
        "  --tek-ekran       Tablet bağlıyken diğer monitörleri kapat (tablet Windows'un tek ekranı olur)\n"
        "  --ekran N         Yansıtılacak PC ekranı (0, 1, ...)\n"
        "  --fare            Dokunmayı fare tıklaması olarak ilet (varsayılan: çoklu dokunmatik ekran)\n"
        "  --fps N           En yüksek kare hızı (varsayılan 60)\n"
        "  --dusuk-gecikme   Hareket sırasında da itici kare (gecikme ~20 ms, tablet ~2 kat daha çok çalışır)\n"
        "  --bitrate KBPS    Bit hızı (varsayılan 8000)\n"
        "  --itici N         Değişimden sonraki itici kare sayısı (varsayılan 3, 0 = kapalı)\n"
        "  --itici-ara MS    İtici kareler arası süre (varsayılan 8)\n");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](int& v) {
            if (i + 1 >= argc) return false;
            v = std::atoi(argv[++i]);
            return true;
        };
        if (a == "--usb") o.mode = LinkMode::Usb;
        else if (a == "--wifi") o.mode = LinkMode::Wifi;
        else if (a == "--ip" && i + 1 < argc) { o.ip = argv[++i]; o.mode = LinkMode::Wifi; }
        else if (a == "--seri" && i + 1 < argc) { o.serial = argv[++i]; o.mode = LinkMode::Usb; }
        else if (a == "--ekran") { if (!next(o.output)) return false; }
        else if (a == "--fps") { if (!next(o.fps)) return false; }
        else if (a == "--bitrate") { if (!next(o.kbps)) return false; }
        else if (a == "--itici") { if (!next(o.pushers)) return false; }
        else if (a == "--itici-ara") { if (!next(o.pusherMs)) return false; }
        else if (a == "--yansit") o.extend = false;
        else if (a == "--tek-ekran") o.onlyTablet = true;
        else if (a == "--fare") o.touchAsMouse = true;
        else if (a == "--dusuk-gecikme") o.lowLatency = true;
        else return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    // DLL yalnızca System32'den ve exe'nin klasöründen (geçerli klasör ve PATH'teki DLL'ler yüklenmesin)
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    SetConsoleOutputCP(CP_UTF8);
    Options opt;
    if (!parse(argc, argv, opt)) {
        usage();
        return 2;
    }
    // Fiziksel piksel koordinatları (dokunma eşlemesi ölçeklenmiş ekranlarda da doğru olsun)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // Ölçüm satırı \r ile aynı satırda güncellenir; günlük satırları onu yeni satıra iter.
    setLogSink([](const std::string& s) { std::printf("\r%-110s\n", s.c_str()); std::fflush(stdout); });

    StreamerEvents ev;
    // Birden çok cihaz bağlıyken her satırın başında cihaz anahtarı
    ev.onStats = [](const std::string& key, const std::string& line) {
        std::printf("\r%s  %s   ", key.c_str(), line.c_str());
        std::fflush(stdout);
    };
    ev.onDevices = [](const std::vector<DeviceInfo>& list) {
        static const char* states[] = {"bulundu", "bağlanıyor", "bağlı", "cihazda açılması bekleniyor", "uygulama yok"};
        std::string s;
        for (const auto& d : list)
            s += (s.empty() ? "" : " · ") + d.name + " (" + d.link + ", " + states[int(d.state) % 5] + ")";
        logf("Cihazlar: %s", s.empty() ? "yok" : s.c_str());
    };
    ev.onPairRequest = [](const std::string& device, const std::string& code, HANDLE) {
        std::printf("\n\n  Eşleştirme: %s\n  Tabletteki kod:  %s\n  Aynı mı? Onaylıyor musunuz? (e/h): ", device.c_str(), code.c_str());
        std::fflush(stdout);
        char buf[16] = {};
        if (!std::fgets(buf, sizeof buf, stdin)) return false;
        return buf[0] == 'e' || buf[0] == 'E' || buf[0] == 'y' || buf[0] == 'Y';
    };
    // Hizmet çalışıyorsa tablete iki yayıncı bağlanmaya çalışmasın
    if (SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)) {
        SERVICE_STATUS st{};
        SC_HANDLE svc = OpenServiceW(scm, L"EkranLink", SERVICE_QUERY_STATUS);
        const bool running = svc && QueryServiceStatus(svc, &st) && st.dwCurrentState != SERVICE_STOPPED;
        if (svc) CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (running) {
            logf("EkranLink hizmeti çalışıyor; yayını o yapıyor. Konsol için önce durdurun: sc stop EkranLink");
            return 1;
        }
    }
    Streamer streamer(opt, ev);
    g_streamer = &streamer;
    SetConsoleCtrlHandler(onCtrl, TRUE);

    const char* mode = opt.mode == LinkMode::Usb ? "USB" : opt.mode == LinkMode::Wifi ? "Wi-Fi" : "otomatik";
    logf("EkranLink sunucusu — %s%s%s. Tablet bekleniyor... (Ctrl+C: çıkış)", mode, opt.ip.empty() ? "" : " ",
         opt.ip.c_str());
    streamer.run();
    g_streamer = nullptr;

    WSACleanup();
    MFShutdown();
    CoUninitialize();
    return 0;
}
