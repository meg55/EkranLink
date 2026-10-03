// Kullanıcının gördüğü metinler: Türkçe ve İngilizce. Günlük (log) satırları Türkçe kalır.
// Dil ayarı "Language": 0 sistem (Windows görüntü dili), 1 Türkçe, 2 İngilizce.
#pragma once

#include <string>

namespace ekran::i18n {

// X(ad, Türkçe, İngilizce)
#define EKRAN_STRINGS(X)                                                                                            \
    X(Waiting, L"Tablet bekleniyor", L"Waiting for a tablet")                                                       \
    X(Connected, L"Bağlı", L"Connected")                                                                            \
    X(WaitingHint, L"Tablette EKRAN'ı açın; USB ile takın ya da aynı Wi-Fi ağına bağlayın.",                        \
      L"Open EKRAN on the tablet, then plug it in over USB or join the same Wi-Fi network.")                       \
    X(NotifyConnected, L"Tablet bağlandı", L"Tablet connected")                                                     \
    X(NotifyDisconnected, L"Tablet ayrıldı", L"Tablet disconnected")                                                \
    X(ModeExtendShort, L"ikinci ekran", L"extended display")                                                        \
    X(ModeOnlyShort, L"tek ekran", L"tablet only")                                                                  \
    X(ModeMirrorShort, L"yansıtma", L"mirroring")                                                                   \
    X(Encrypted, L"şifreli", L"encrypted")                                                                          \
    X(Latency, L"gecikme", L"latency")                                                                              \
    X(MenuOpen, L"EKRAN'ı aç", L"Open EKRAN")                                                                       \
    X(MenuExit, L"Çıkış", L"Exit")                                                                                  \
    X(SecConnection, L"Bağlantı", L"Connection")                                                                    \
    X(LinkType, L"Bağlantı türü", L"Connection type")                                                               \
    X(LinkAuto, L"Otomatik", L"Automatic")                                                                          \
    X(LinkUsb, L"USB", L"USB")                                                                                      \
    X(LinkWifi, L"Wi-Fi", L"Wi-Fi")                                                                                 \
    X(LinkHint, L"Otomatik: önce USB'deki tablet, yoksa aynı ağdaki tablet.",                                       \
      L"Automatic: a tablet on USB first, otherwise one on the same network.")                                      \
    X(AutoConnect, L"Tablet bulununca otomatik bağlan", L"Connect automatically when a tablet is found")            \
    X(AutoConnectHint, L"Kapalıyken yalnızca \"Şimdi bağlan\" ile bağlanır.",                                       \
      L"When off, it connects only when you press \"Connect now\".")                                                \
    X(ConnectNow, L"Şimdi bağlan", L"Connect now")                                                                  \
    X(SecDisplay, L"Görüntü", L"Display")                                                                           \
    X(DisplayMode, L"Mod", L"Mode")                                                                                 \
    X(ModeExtend, L"İkinci ekran", L"Extend")                                                                       \
    X(ModeOnly, L"Tek ekran", L"Tablet only")                                                                       \
    X(ModeMirror, L"Yansıtma", L"Mirror")                                                                           \
    X(HintExtend, L"Tablet ayrı bir monitör olur.", L"The tablet becomes a separate monitor.")                      \
    X(HintOnly, L"Tablet bağlıyken diğer monitörler kapanır; tablet ayrılınca geri gelir.",                         \
      L"Other monitors turn off while the tablet is connected and come back when it disconnects.")                 \
    X(HintMirror, L"Seçili bilgisayar ekranının kopyası tablette görünür.",                                         \
      L"The tablet shows a copy of the selected computer screen.")                                                  \
    X(DriverMissing, L"Sanal ekran sürücüsü kurulu değil; yalnızca yansıtma kullanılabilir.",                       \
      L"The virtual display driver is not installed; only mirroring is available.")                                 \
    X(Orientation, L"Monitör yönü", L"Orientation")                                                                 \
    X(OrientAuto, L"Otomatik", L"Automatic")                                                                        \
    X(OrientLandscape, L"Yatay", L"Landscape")                                                                      \
    X(OrientPortrait, L"Dikey", L"Portrait")                                                                        \
    X(OrientLandscapeFlipped, L"Yatay (ters)", L"Landscape (flipped)")                                              \
    X(OrientPortraitFlipped, L"Dikey (ters)", L"Portrait (flipped)")                                                \
    X(OrientHint,                                                                                                   \
      L"Otomatik: tablet çevrilince monitör de döner (tablette otomatik döndürme açık olmalı). Seçiminiz ve "      \
      L"Windows ekran ayarlarından yaptığınız değişiklik hatırlanır.",                                             \
      L"Automatic: the monitor rotates when you turn the tablet (auto-rotate must be on). Your choice and "        \
      L"changes made in Windows display settings are remembered.")                                                  \
    X(OrientMirrorHint, L"Yansıtmada yön, bilgisayar ekranının kendi yönüdür.",                                     \
      L"When mirroring, the orientation is that of the computer screen.")                                          \
    X(MirroredScreen, L"Yansıtılan ekran", L"Mirrored screen")                                                      \
    X(Resolution, L"Çözünürlük", L"Resolution")                                                                     \
    X(ResPercent, L"%%%d", L"%d%%")                                                                                 \
    X(ResPercentSize, L"%%%d  ·  %d×%d", L"%d%%  ·  %d×%d")                                                         \
    X(ResNative, L"  (tabletin kendi çözünürlüğü)", L"  (tablet's native resolution)")                             \
    X(ResHint,                                                                                                      \
      L"Yüksek çözünürlük daha çok çalışma alanı verir, yazılar küçülür; düşükte yazılar büyür. Windows ekran "     \
      L"ayarlarından yaptığınız değişiklik de hatırlanır.",                                                         \
      L"A higher resolution gives more workspace with smaller text; a lower one makes text larger. Changes made "  \
      L"in Windows display settings are remembered too.")                                                           \
    X(ResMirrorHint, L"Yansıtmada çözünürlük, bilgisayar ekranının kendi çözünürlüğüdür.",                          \
      L"When mirroring, the resolution is that of the computer screen.")                                           \
    X(ScreenN, L"Ekran %d  (%d×%d)", L"Screen %d  (%d×%d)")                                                         \
    X(SecInput, L"Dokunma ve performans", L"Touch and performance")                                                 \
    X(Touch, L"Dokunma", L"Touch")                                                                                  \
    X(TouchScreen, L"Dokunmatik ekran", L"Touchscreen")                                                             \
    X(TouchMouse, L"Fare", L"Mouse")                                                                                \
    X(TouchHint, L"Dokunmatik ekran: çoklu dokunma ve hareketler. Fare: tek parmak sol tık.",                       \
      L"Touchscreen: multi-touch and gestures. Mouse: one finger is a left click.")                                \
    X(Performance, L"Performans", L"Performance")                                                                   \
    X(PerfBalanced, L"Dengeli", L"Balanced")                                                                        \
    X(PerfLowLatency, L"Düşük gecikme", L"Low latency")                                                             \
    X(PerfHint, L"Dengeli az işlemci ve pil harcar. Düşük gecikme tableti daha çok çalıştırır.",                    \
      L"Balanced uses little CPU and battery. Low latency makes the tablet work harder.")                          \
    X(SecGeneral, L"Genel", L"General")                                                                             \
    X(StartWithWindows, L"Windows ile başlat", L"Start with Windows")                                               \
    X(Service, L"Oturum açılmadan önce başlat", L"Start before sign-in")                                            \
    X(ServiceHint, L"Kilit ekranı, oturum açma ve UAC pencereleri de tablette görünür (yönetici izni ister).",      \
      L"The lock screen, sign-in and UAC prompts also appear on the tablet (asks for administrator permission).")  \
    X(ServiceStale, L"Kurulu hizmet eski bir sürüm.", L"The installed service is an older version.")                \
    X(ServiceUpdate, L"Hizmeti güncelle", L"Update service")                                                        \
    X(Language, L"Dil", L"Language")                                                                                \
    X(LangSystem, L"Sistem", L"System")                                                                             \
    X(OpenLog, L"Günlüğü aç", L"Open log")                                                                          \
    X(Forget, L"Eşleşmeleri unut", L"Forget paired tablets")                                                        \
    X(Version, L"Sürüm %ls", L"Version %ls")                                                                        \
    X(ForgetConfirm, L"Tüm eşleşmiş tabletler unutulsun mu?\nWi-Fi ile bağlanırken yeniden kod onayı istenir.",     \
      L"Forget all paired tablets?\nA code confirmation will be needed again when connecting over Wi-Fi.")         \
    X(ServiceRemoveConfirm,                                                                                         \
      L"EKRAN hizmeti kaldırılsın mı?\n\nOturum açma ekranı ve UAC pencereleri artık tablette görünmez; yayını "  \
      L"yine bu uygulama yapar.",                                                                                   \
      L"Remove the EKRAN service?\n\nThe sign-in screen and UAC prompts will no longer appear on the tablet; this " \
      L"app keeps streaming.")                                                                                      \
    X(ServiceFailed, L"Hizmet kurulamadı ya da kaldırılamadı.\nAyrıntı: C:\\ProgramData\\EKRAN\\hizmet.log",        \
      L"Could not install or remove the service.\nDetails: C:\\ProgramData\\EKRAN\\hizmet.log")                    \
    X(ServiceInstalled, L"Tablet artık oturum açma ekranını da gösterecek.",                                        \
      L"The tablet will now also show the sign-in screen.")                                                         \
    X(ServiceRemoved, L"Hizmet kaldırıldı; yayını bu uygulama yapıyor.",                                            \
      L"Service removed; this app is streaming now.")                                                               \
    X(PairTitle, L"EKRAN — eşleştirme", L"EKRAN — pairing")                                                         \
    X(PairText,                                                                                                     \
      L"\"%ls\" bu bilgisayarla eşleşmek istiyor.\n\nTablette görünen kod:\n\n        %ls\n\nKodlar aynıysa "      \
      L"Evet'e basın ve tablette de onaylayın.\nFarklıysa Hayır'a basın: ağda başka biri araya girmeye "           \
      L"çalışıyor olabilir.",                                                                                       \
      L"\"%ls\" wants to pair with this computer.\n\nCode shown on the tablet:\n\n        %ls\n\nIf the codes "    \
      L"match, press Yes and confirm on the tablet too.\nIf they differ, press No: someone on the network may "    \
      L"be trying to intercept the connection.")                                                                    \
    X(NoticeAppMissingTitle, L"Tablette EKRAN uygulaması yok", L"EKRAN app not found on the tablet")                \
    X(NoticeAppMissing,                                                                                             \
      L"EKRAN'ın Android uygulamasını (APK) tablete indirip kurun. Kurulunca bağlantı kendiliğinden kurulur.",     \
      L"Download and install the EKRAN Android app (APK) on the tablet. It connects by itself once installed.")    \
    X(NoticeTabletOldTitle, L"Tabletteki EKRAN eski", L"EKRAN on the tablet is outdated")                           \
    X(NoticeTabletOld, L"Tablete EKRAN'ın Android uygulamasının yeni sürümünü (APK) kurun.",                        \
      L"Install the new version of the EKRAN Android app (APK) on the tablet.")                                     \
    X(NoticePcOldTitle, L"Bu bilgisayardaki EKRAN eski", L"EKRAN on this computer is outdated")                     \
    X(NoticePcOld, L"Bilgisayara EKRAN'ın yeni sürümünü kurun (tablette %ls var).",                                 \
      L"Install the new version of EKRAN on this computer (the tablet has %ls).")

enum Str : int {
#define EKRAN_ENUM(name, tr, en) name,
    EKRAN_STRINGS(EKRAN_ENUM)
#undef EKRAN_ENUM
        kStrCount
};

enum class Lang { Tr, En };

// setting: 0 sistem, 1 Türkçe, 2 İngilizce
void setLanguage(int setting);
Lang language();
// Windows görüntü dili (kullanıcının): Türkçe değilse İngilizce
Lang systemLanguage();

const wchar_t* W(Str id);
// printf biçimli metin (%ls, %d)
std::wstring F(Str id, ...);

}  // namespace ekran::i18n
