// EKRAN sunucusu — konsol sürümü (geliştirme ve tanı için). Günlük kullanım: ekran.exe (tepsi).

#include <winsock2.h>
#include <windows.h>
#include <mfapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "adb.h"
#include "apkserver.h"
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
        "Kullanım: ekran-host [seçenekler]\n"
        "  (seçeneksiz)      Otomatik: USB'deki tablet, yoksa Wi-Fi'de aranan tablet\n"
        "  --usb             Sadece USB (adb)\n"
        "  --wifi            Sadece Wi-Fi (ağda otomatik aranır)\n"
        "  --ip ADRES        Wi-Fi ile bu IP'deki tablete bağlan\n"
        "  --yansit          İkinci ekran yerine mevcut ekranı yansıt (sürücü kurulu değilse zaten yansıtır)\n"
        "  --tek-ekran       Tablet bağlıyken diğer monitörleri kapat (tablet Windows'un tek ekranı olur)\n"
        "  --ekran N         Yansıtılacak PC ekranı (0, 1, ...)\n"
        "  --fare            Dokunmayı fare tıklaması olarak ilet (varsayılan: çoklu dokunmatik ekran)\n"
        "  --fps N           En yüksek kare hızı (varsayılan 60)\n"
        "  --dusuk-gecikme   Hareket sırasında da itici kare (gecikme ~20 ms, tablet ~2 kat daha çok çalışır)\n"
        "  --bitrate KBPS    Bit hızı (varsayılan 8000)\n"
        "  --itici N         Değişimden sonraki itici kare sayısı (varsayılan 3, 0 = kapalı)\n"
        "  --itici-ara MS    İtici kareler arası süre (varsayılan 8)\n"
        "  --apk-sun         Tablete uygulamayı Wi-Fi'dan kurmak için 10 dk indirme sayfası aç\n");
}

bool g_serveApk = false;

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
        else if (a == "--ekran") { if (!next(o.output)) return false; }
        else if (a == "--fps") { if (!next(o.fps)) return false; }
        else if (a == "--bitrate") { if (!next(o.kbps)) return false; }
        else if (a == "--itici") { if (!next(o.pushers)) return false; }
        else if (a == "--itici-ara") { if (!next(o.pusherMs)) return false; }
        else if (a == "--apk-sun") g_serveApk = true;
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
    ev.onStats = [](const std::string& line) { std::printf("\r%s   ", line.c_str()); std::fflush(stdout); };
    ev.onPairRequest = [](const std::string& device, const std::string& code, HANDLE) {
        std::printf("\n\n  Eşleştirme: %s\n  Tabletteki kod:  %s\n  Aynı mı? Onaylıyor musunuz? (e/h): ", device.c_str(), code.c_str());
        std::fflush(stdout);
        char buf[16] = {};
        if (!std::fgets(buf, sizeof buf, stdin)) return false;
        return buf[0] == 'e' || buf[0] == 'E' || buf[0] == 'y' || buf[0] == 'Y';
    };
    Streamer streamer(opt, ev);
    g_streamer = &streamer;
    SetConsoleCtrlHandler(onCtrl, TRUE);

    const char* mode = opt.mode == LinkMode::Usb ? "USB" : opt.mode == LinkMode::Wifi ? "Wi-Fi" : "otomatik";
    logf("EKRAN sunucusu — %s%s%s. Tablet bekleniyor... (Ctrl+C: çıkış)", mode, opt.ip.empty() ? "" : " ",
         opt.ip.c_str());
    ApkServer apkServer;
    if (g_serveApk) {
        const std::wstring apk = adb::bundledApk();
        if (apk.empty()) logf("APK bulunamadı (ekran-android.apk exe'nin yanında olmalı)");
        else if (apkServer.start(apk, ApkServer::kPort, 10))
            for (const auto& u : apkServer.urls()) logf("Tabletin tarayıcısında açın: %s", u.c_str());
    }
    streamer.run();
    g_streamer = nullptr;

    WSACleanup();
    MFShutdown();
    CoUninitialize();
    return 0;
}
