// Kullanıcının gördüğü metinler: Türkçe ve İngilizce. Günlük (log) satırları Türkçe kalır.
// Dil ayarı "Language": 0 sistem (Windows görüntü dili), 1 Türkçe, 2 İngilizce.
#pragma once

#include <string>

namespace ekran::i18n {

// X(ad, Türkçe, İngilizce)
#define EKRAN_STRINGS(X)                                                                                            \
    X(Waiting, L"Cihaz bekleniyor", L"Waiting for a device")                                                        \
    X(Connected, L"Bağlı", L"Connected")                                                                            \
    X(WaitingHint, L"Tablette ya da telefonda EkranLink'i açın ve cihazı bu bilgisayarla aynı Wi-Fi ağına bağlayın "  \
      L"(ya da USB ile takın).",                                                                                    \
      L"Open EkranLink on the tablet or phone and join it to the same Wi-Fi network as this computer (or plug it " \
      L"in over USB).")                                                                                             \
    X(NotifyConnected, L"Cihaz bağlandı", L"Device connected")                                                      \
    X(NotifyDisconnected, L"Cihaz ayrıldı", L"Device disconnected")                                                 \
    X(ModeExtendShort, L"ikinci ekran", L"extended display")                                                        \
    X(ModeOnlyShort, L"tek ekran", L"only display")                                                                 \
    X(ModeMirrorShort, L"yansıtma", L"mirroring")                                                                   \
    X(Encrypted, L"şifreli", L"encrypted")                                                                          \
    X(Latency, L"gecikme", L"latency")                                                                              \
    X(MenuOpen, L"EkranLink'i aç", L"Open EkranLink")                                                                       \
    X(MenuExit, L"Çıkış", L"Exit")                                                                                  \
    X(NavStatus, L"Durum", L"Status")                                                                               \
    X(RowLink, L"Bağlantı", L"Connection")                                                                          \
    X(RowMode, L"Görüntü modu", L"Display mode")                                                                    \
    X(RowScreen, L"Ekran", L"Screen")                                                                               \
    X(RowLatency, L"Gecikme", L"Latency")                                                                           \
    X(RowData, L"Veri", L"Data")                                                                                    \
    X(RowFps, L"Kare hızı", L"Frame rate")                                                                          \
    X(RowPerf, L"Ölçümler", L"Performance")                                                                         \
    X(StatsPending, L"ölçülüyor…", L"measuring…")                                                                   \
    X(StepsTitle, L"Bağlanmak için", L"To connect")                                                                 \
    X(StepApp, L"1.  Cihaza EkranLink'in Android uygulamasını (APK) kurun ve açın.",                                    \
      L"1.  Install the EkranLink Android app (APK) on the device and open it.")                                       \
    X(StepWifi, L"2.  Cihazı bu bilgisayarla aynı Wi-Fi ağına bağlayın. Listede görününce Bağlan'a tıklayın ve iki " \
      L"ekranda aynı kodu onaylayın (yalnızca ilk seferde; sonra kendiliğinden bağlanır).",                         \
      L"2.  Join the device to the same Wi-Fi network as this computer. When it appears in the list, click "       \
      L"Connect and confirm the same code on both screens (first time only; after that it connects by itself).")    \
    X(StepUsb, L"3.  İsteğe bağlı, en düşük gecikme için USB kablosu: cihazda Geliştirici seçenekleri > USB hata "    \
      L"ayıklama açık olmalı (görünmüyorsa Ayarlar > Telefon/Tablet hakkında > Yapı numarası'na 7 kez dokunun).",   \
      L"3.  Optional, for the lowest delay, a USB cable: the device needs Developer options > USB debugging "       \
      L"(not shown? Tap Settings > About phone/tablet > Build number 7 times).")                                    \
    X(DownloadApp, L"Android uygulamasını indir", L"Download the Android app")                                      \
    X(SecConnection, L"Bağlantı", L"Connection")                                                                    \
    X(LinkType, L"Bağlantı türü", L"Connection type")                                                               \
    X(LinkAuto, L"Otomatik", L"Automatic")                                                                          \
    X(LinkUsb, L"USB", L"USB")                                                                                      \
    X(LinkWifi, L"Wi-Fi", L"Wi-Fi")                                                                                 \
    X(LinkHint, L"Otomatik: USB'deki ve aynı ağdaki cihazlar; aynı cihaz ikisinde de varsa USB.",                  \
      L"Automatic: devices on USB and on the same network; USB if a device is on both.")                           \
    X(AutoConnect, L"Cihaz bulununca otomatik bağlan", L"Connect automatically when a device is found")             \
    X(AutoConnectHint, L"Kapalıyken Durum sayfasındaki listeden \"Bağlan\" ile bağlanılır. Bağlantısını "        \
      L"kestiğiniz cihaza, siz yeniden bağlanana ya da cihaz çıkarılana kadar otomatik bağlanılmaz.",               \
      L"When off, connect from the list on the Status page. A device you disconnect is not reconnected "          \
      L"automatically until you connect it again or unplug it.")                                                    \
    X(Connect, L"Bağlan", L"Connect")                                                                               \
    X(Disconnect, L"Bağlantıyı kes", L"Disconnect")                                                                 \
    X(DisconnectAll, L"Tümünün bağlantısını kes", L"Disconnect all")                                                \
    X(DevicesConnected, L"%d cihaz bağlı", L"%d devices connected")                                                 \
    X(FoundDevices, L"Bağlanabilecek cihazlar", L"Available devices")                                               \
    X(StateFound, L"Bağlanmaya hazır", L"Ready to connect")                                                         \
    X(StateConnecting, L"Bağlanıyor…", L"Connecting…")                                                              \
    X(StateWaitingApp, L"Cihazda EkranLink'i açın", L"Open EkranLink on the device")                                \
    X(StateAppMissing, L"Cihazda EkranLink kurulu değil", L"EkranLink is not installed on the device")              \
    X(StateUnauthorized, L"Cihazda \"USB hata ayıklamaya izin verilsin mi?\" sorusunda İzin ver'e dokunun",          \
      L"On the device, tap Allow on \"Allow USB debugging?\"")                                                     \
    X(StatePaused, L"Duraklatıldı: cihazda EkranLink arka planda. Uygulamaya dönünce görüntü sürer.",              \
      L"Paused: EkranLink is in the background on the device. The picture resumes when you return to the app.")    \
    X(DisplayDevice, L"Cihaz", L"Device")                                                                           \
    X(MenuConnectTo, L"Bağlan: %ls", L"Connect: %ls")                                                               \
    X(MenuDisconnectFrom, L"Bağlantıyı kes: %ls", L"Disconnect: %ls")                                               \
    X(SecDisplay, L"Görüntü", L"Display")                                                                           \
    X(DisplayMode, L"Görüntü modu", L"Display mode")                                                                \
    X(ModeExtend, L"İkinci ekran", L"Extend")                                                                       \
    X(ModeOnly, L"Tek ekran", L"Only this")                                                                         \
    X(ModeMirror, L"Yansıtma", L"Mirror")                                                                           \
    X(HintExtend, L"Cihaz ayrı bir monitör olur (Windows'taki \"Bu ekranları genişlet\").",                         \
      L"The device becomes a separate monitor (\"Extend these displays\" in Windows).")                            \
    X(HintOnly, L"Cihaz bağlıyken diğer monitörler kapanır; cihaz ayrılınca geri gelir.",                           \
      L"Other monitors turn off while the device is connected and come back when it disconnects.")                 \
    X(HintMirror, L"Seçili bilgisayar ekranının kopyası cihazda görünür.",                                          \
      L"The device shows a copy of the selected computer screen.")                                                  \
    X(DriverMissing, L"İkinci ekran ve Tek ekran için sanal ekran sürücüsü gerekir; bu sürümde henüz yok (kod "     \
      L"imzası tamamlanınca gelecek). Şimdilik yansıtma kullanılabilir.",                                           \
      L"Extend and Only this need the virtual display driver, which this release does not include yet (it comes "  \
      L"once it is code-signed). Mirroring works now.")                                                             \
    X(Orientation, L"Ekran yönü", L"Display orientation")                                                           \
    X(OrientAuto, L"Otomatik (cihazı çevirince döner)", L"Automatic (follows the device)")                          \
    X(OrientLandscape, L"Yatay", L"Landscape")                                                                      \
    X(OrientPortrait, L"Dikey", L"Portrait")                                                                        \
    X(OrientLandscapeFlipped, L"Yatay (ters çevrilmiş)", L"Landscape (flipped)")                                    \
    X(OrientPortraitFlipped, L"Dikey (ters çevrilmiş)", L"Portrait (flipped)")                                      \
    X(Resolution, L"Ekran çözünürlüğü", L"Display resolution")                                                      \
    X(ResItem, L"%d × %d", L"%d × %d")                                                                              \
    X(Recommended, L" (Önerilen)", L" (Recommended)")                                                               \
    X(ResUnavailable, L"Cihaz bağlanınca seçilebilir", L"Available once a device is connected")                     \
    X(DisplaySyncHint,                                                                                              \
      L"Yön ve çözünürlük, Windows Ayarlar > Ekran'daki ayarların aynısıdır: birinden yaptığınız değişiklik "      \
      L"diğerinde de görünür ve bu cihaz için hatırlanır. Önerilen, cihazın kendi çözünürlüğüdür; daha "           \
      L"yükseği daha çok alan verir ama yazılar küçülür.",                                                          \
      L"Orientation and resolution are the same settings as in Windows Settings > Display: a change in either "    \
      L"place shows up in the other and is remembered for this device. Recommended is the device's own "           \
      L"resolution; a higher one gives more space but smaller text.")                                               \
    X(OpenDisplaySettings, L"Windows ekran ayarları", L"Windows display settings")                                  \
    X(MirroredScreen, L"Yansıtılan ekran", L"Mirrored screen")                                                      \
    X(ScreenN, L"Ekran %d  (%d × %d)", L"Screen %d  (%d × %d)")                                                     \
    X(SecInput, L"Dokunma ve performans", L"Touch and performance")                                                 \
    X(Touch, L"Dokunma", L"Touch")                                                                                  \
    X(TouchScreen, L"Dokunmatik ekran", L"Touchscreen")                                                             \
    X(TouchMouse, L"Fare", L"Mouse")                                                                                \
    X(TouchHint, L"Dokunmatik ekran: çoklu dokunma ve hareketler. Fare: tek parmak sol tık.",                       \
      L"Touchscreen: multi-touch and gestures. Mouse: one finger is a left click.")                                \
    X(Performance, L"Performans", L"Performance")                                                                   \
    X(PerfBalanced, L"Dengeli", L"Balanced")                                                                        \
    X(PerfLowLatency, L"Düşük gecikme", L"Low latency")                                                             \
    X(PerfHint, L"Dengeli az işlemci ve pil harcar. Düşük gecikme cihazı daha çok çalıştırır.",                    \
      L"Balanced uses little CPU and battery. Low latency makes the device work harder.")                          \
    X(SecGeneral, L"Genel", L"General")                                                                             \
    X(StartWithWindows, L"Windows ile başlat", L"Start with Windows")                                               \
    X(Service, L"Oturum açılmadan önce başlat", L"Start before sign-in")                                            \
    X(ServiceHint, L"Kilit ekranı, oturum açma ve UAC pencereleri de cihazda görünür (yönetici izni ister).",      \
      L"The lock screen, sign-in and UAC prompts also appear on the device (asks for administrator permission).")  \
    X(ServiceStale, L"Kurulu hizmet eski bir sürüm.", L"The installed service is an older version.")                \
    X(ServiceUpdate, L"Hizmeti güncelle", L"Update service")                                                        \
    X(Language, L"Dil", L"Language")                                                                                \
    X(LangSystem, L"Sistem", L"System")                                                                             \
    X(OpenLog, L"Günlüğü aç", L"Open log")                                                                          \
    X(Forget, L"Eşleşmeleri unut", L"Forget paired devices")                                                        \
    X(Version, L"Sürüm %ls", L"Version %ls")                                                                        \
    X(ForgetConfirm, L"Tüm eşleşmiş cihazlar unutulsun mu?\nWi-Fi ile bağlanırken yeniden kod onayı istenir.",     \
      L"Forget all paired devices?\nA code confirmation will be needed again when connecting over Wi-Fi.")         \
    X(ServiceRemoveConfirm,                                                                                         \
      L"EkranLink hizmeti kaldırılsın mı?\n\nOturum açma ekranı ve UAC pencereleri artık cihazda görünmez; yayını "  \
      L"yine bu uygulama yapar.",                                                                                   \
      L"Remove the EkranLink service?\n\nThe sign-in screen and UAC prompts will no longer appear on the device; this " \
      L"app keeps streaming.")                                                                                      \
    X(ServiceFailed, L"Hizmet kurulamadı ya da kaldırılamadı.\nAyrıntı: C:\\ProgramData\\EkranLink\\hizmet.log",        \
      L"Could not install or remove the service.\nDetails: C:\\ProgramData\\EkranLink\\hizmet.log")                    \
    X(ServiceDisplayName, L"EkranLink tablet ekranı", L"EkranLink tablet display")                                  \
    X(ServiceDescription,                                                                                           \
      L"Android tablet ve telefonları bu bilgisayarın ekranı yapar; oturum açma ekranı ve UAC dahil.",              \
      L"Makes Android tablets and phones a display for this computer, including the sign-in screen and UAC.")      \
    X(ServiceInstalled, L"Cihaz artık oturum açma ekranını da gösterecek.",                                        \
      L"The device will now also show the sign-in screen.")                                                         \
    X(ServiceRemoved, L"Hizmet kaldırıldı; yayını bu uygulama yapıyor.",                                            \
      L"Service removed; this app is streaming now.")                                                               \
    X(PairTitle, L"EkranLink — eşleştirme", L"EkranLink — pairing")                                                         \
    X(PairText,                                                                                                     \
      L"\"%ls\" bu bilgisayarla eşleşmek istiyor.\n\nCihazda görünen kod:\n\n        %ls\n\nKodlar aynıysa "      \
      L"Evet'e basın ve cihazda da onaylayın.\nFarklıysa ya da bu cihazı siz bağlamadıysanız Hayır'a basın: "      \
      L"başka bir cihaz ya da program araya girmeye çalışıyor olabilir.\n\nBu soru her cihaz için bir kez sorulur.", \
      L"\"%ls\" wants to pair with this computer.\n\nCode shown on the device:\n\n        %ls\n\nIf the codes "    \
      L"match, press Yes and confirm on the device too.\nIf they differ, or you did not connect this device, "    \
      L"press No: another device or program may be trying to intercept the connection.\n\nYou are asked only "    \
      L"once per device.")                                                                                          \
    X(NoticeAppMissingTitle, L"Cihazda EkranLink uygulaması yok", L"EkranLink app not found on the device")                \
    X(NoticeAppMissing,                                                                                             \
      L"EkranLink'in Android uygulamasını (APK) cihaza indirip kurun. Kurulunca bağlantı kendiliğinden kurulur.",     \
      L"Download and install the EkranLink Android app (APK) on the device. It connects by itself once installed.")    \
    X(NoticePairDeclinedTitle, L"Eşleştirme onaylanmadı", L"Pairing was not confirmed")                             \
    X(NoticePairDeclined,                                                                                           \
      L"%ls cihazında eşleştirme reddedildi ya da süresi doldu. Yeniden denemek için Bağlan'a basın ve iki "        \
      L"ekranda aynı kodu onaylayın.",                                                                              \
      L"Pairing was declined or timed out on %ls. To try again, click Connect and confirm the same code on both "  \
      L"screens.")                                                                                                  \
    X(NoticeNewDeviceTitle, L"Yeni cihaz bulundu", L"New device found")                                             \
    X(NoticeNewDevice, L"%ls bağlanmaya hazır. Bağlanmak için buraya tıklayın.",                                    \
      L"%ls is ready to connect. Click here to connect.")                                                           \
    X(NoticeTabletOldTitle, L"Cihazdaki EkranLink eski", L"EkranLink on the device is outdated")                           \
    X(NoticeTabletOld, L"Cihaza EkranLink'in Android uygulamasının yeni sürümünü (APK) kurun.",                        \
      L"Install the new version of the EkranLink Android app (APK) on the device.")                                     \
    X(NoticePcOldTitle, L"Bu bilgisayardaki EkranLink eski", L"EkranLink on this computer is outdated")                     \
    X(NoticePcOld, L"Bilgisayara EkranLink'in yeni sürümünü kurun (cihazda %ls var).",                                 \
      L"Install the new version of EkranLink on this computer (the device has %ls).")                                 \
    X(NoticeRepairTitle, L"Cihaz yeniden eşleştirilmeli", L"The device needs to be paired again")                        \
    X(NoticeRepair,                                                                                                 \
      L"%ls doğrulanamadı: iki taraftaki eşleştirme bilgileri uyuşmuyor. Cihazı USB'ye takın ya da pencerede "    \
      L"Bağlan'a basın, sonra iki ekranda aynı kodu onaylayın.",                                                     \
      L"%ls could not be verified: the pairing details on the two sides do not match. Plug the device in over "    \
      L"USB or click Connect in the window, then confirm the same code on both screens.")

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
