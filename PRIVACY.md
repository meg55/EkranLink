# Privacy policy / Gizlilik politikası

**English** · [Türkçe](#türkçe)

*Effective date: 13 October 2026.* This policy covers the EkranLink Windows app, its virtual display driver and
the EkranLink Android app.

## Summary

EkranLink does not collect personal data. It has no accounts, no analytics, no advertising, no crash reporting
and it does not connect to the internet. Data only travels between your own computer and your own Android
devices, and only when you connect them.

## What travels between your devices

- **PC → device:** the picture of the PC screen (H.264 video), the mouse cursor shape and position, and
  whether the PC's screens are on.
- **Device → PC:** touches, how the device is held (orientation), whether the app is in the foreground,
  acknowledgements of received frames, and basic device information (model name, Android version, screen
  size, decoder name, an identifier described below).
- Over Wi-Fi everything is encrypted and authenticated (ECDH P-256 + ChaCha20-Poly1305). Over USB the
  connection runs through the cable (adb); the picture to the device is not encrypted, everything from the
  device to the PC is. See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Finding devices on your network

To find devices over Wi-Fi, the PC sends a small query (UDP, port 27183) to the local network it is
connected to, and to the addresses of devices it connected to before. The Android app answers only while it
is open, with its name and identifier. These packets do not leave your local network.

## What is stored, and where

- **On the PC:** settings and pairing keys in the Windows registry (in service mode, pairing keys are
  readable only by the system and administrators), and log files in `%LOCALAPPDATA%\EkranLink` or
  `%ProgramData%\EkranLink`. Logs contain device names and local IP addresses.
- **On the Android device:** settings and pairing keys in the app's private storage. They are excluded from
  backups and device-to-device transfer.
- **Device identifier:** the Android app uses the device's Android ID (or serial number on very old devices)
  so that your PC recognizes the device and remembers its settings. It is only shared with the PCs you connect.

Nothing is sent to us or to anyone else. Logs leave your computer only if you attach them to a bug report
yourself. Uninstalling the apps removes them; the Windows uninstaller asks whether to delete settings and
pairings too.

## Android permissions

| Permission | Why |
|---|---|
| Internet | Connections to your PC on the local network (and over the USB cable through adb) |
| Change Wi-Fi multicast state | Receiving the PC's device-discovery query on Wi-Fi |
| Wake lock | Keeping the screen and connection alive while used as a monitor |
| Foreground service (connected device) | Keeping the connection when the app is in the background |
| Notifications | Showing the ongoing-connection notification with a *Disconnect* button |

## Third parties

EkranLink includes no third-party services. Where you download it from (GitHub, Google Play) has its own
privacy policy.

## Children

EkranLink collects no data from anyone, including children.

## Changes and contact

Changes to this policy are published in this file with a new effective date. Questions: ekranlink@gmail.com
or [GitHub issues](https://github.com/meg55/EkranLink/issues).

---

## Türkçe

*Yürürlük tarihi: 13 Ekim 2026.* Bu politika EkranLink Windows uygulamasını, sanal ekran sürücüsünü ve EkranLink
Android uygulamasını kapsar.

### Özet

EkranLink kişisel veri toplamaz. Hesap, analiz, reklam ve çökme raporu yoktur; internete bağlanmaz. Veri
yalnızca kendi bilgisayarınızla kendi Android cihazlarınız arasında ve yalnızca onları bağladığınızda gider.

### Cihazlarınız arasında ne gider

- **PC → cihaz:** PC ekranının görüntüsü (H.264 video), fare imlecinin şekli ve konumu, PC ekranlarının açık
  olup olmadığı.
- **Cihaz → PC:** dokunmalar, cihazın nasıl tutulduğu (yön), uygulamanın önde olup olmadığı, alınan karelerin
  onayı ve temel cihaz bilgileri (model adı, Android sürümü, ekran boyutu, çözücü adı, aşağıda anlatılan kimlik).
- Wi-Fi'de her şey şifreli ve doğrulanmıştır (ECDH P-256 + ChaCha20-Poly1305). USB'de bağlantı kablodan geçer
  (adb); cihaza giden görüntü şifrelenmez, cihazdan PC'ye giden her şey şifrelidir. Bkz.
  [docs/PROTOCOL.tr.md](docs/PROTOCOL.tr.md).

### Ağda cihaz bulma

Wi-Fi'de cihaz bulmak için PC bağlı olduğu yerel ağa ve daha önce bağlandığı cihazların adreslerine küçük bir
soru (UDP, port 27183) gönderir. Android uygulaması yalnızca açıkken, adı ve kimliğiyle yanıt verir. Bu paketler
yerel ağınızın dışına çıkmaz.

### Ne, nerede saklanır

- **PC'de:** ayarlar ve eşleştirme anahtarları Windows kayıt defterinde (hizmet modunda eşleştirme anahtarlarını
  yalnızca sistem ve yöneticiler okuyabilir); günlük dosyaları `%LOCALAPPDATA%\EkranLink` ya da
  `%ProgramData%\EkranLink` içinde. Günlükler cihaz adlarını ve yerel IP adreslerini içerir.
- **Android cihazda:** ayarlar ve eşleştirme anahtarları uygulamanın özel alanında; yedeklemeden ve cihazdan
  cihaza aktarımdan hariç tutulur.
- **Cihaz kimliği:** Android uygulaması, PC'nizin cihazı tanıması ve ayarlarını hatırlaması için cihazın
  Android kimliğini (çok eski cihazlarda seri numarasını) kullanır. Yalnızca bağladığınız PC'lerle paylaşılır.

Bize ya da başka birine hiçbir şey gönderilmez. Günlükler bilgisayarınızdan yalnızca siz bir hata bildirimine
eklerseniz çıkar. Uygulamaları kaldırmak onları siler; Windows kaldırıcısı ayarların ve eşleştirmelerin de
silinip silinmeyeceğini sorar.

### Android izinleri

| İzin | Neden |
|---|---|
| İnternet | Yerel ağdaki (ve adb ile USB kablosu üzerinden) PC'nize bağlantı |
| Wi-Fi çoklu yayın durumunu değiştirme | PC'nin Wi-Fi'deki cihaz bulma sorusunu alma |
| Uyanık tutma | Monitör olarak kullanılırken ekranı ve bağlantıyı açık tutma |
| Ön plan hizmeti (bağlı cihaz) | Uygulama arka plandayken bağlantıyı sürdürme |
| Bildirimler | *Bağlantıyı kes* düğmeli bağlantı bildirimini gösterme |

### Üçüncü taraflar

EkranLink hiçbir üçüncü taraf hizmeti içermez. İndirdiğiniz yerin (GitHub, Google Play) kendi gizlilik
politikası vardır.

### Çocuklar

EkranLink, çocuklar dahil kimseden veri toplamaz.

### Değişiklikler ve iletişim

Bu politikadaki değişiklikler yeni bir yürürlük tarihiyle bu dosyada yayımlanır. Sorular: ekranlink@gmail.com ya
da [GitHub issues](https://github.com/meg55/EkranLink/issues).
