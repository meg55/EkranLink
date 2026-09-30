# EKRAN yol haritası

Eski tablet ve telefonları bilgisayar için ekran yapan açık kaynak uygulama: yansıtma, ikinci ekran
ve Windows'un tek ekranı olarak; USB veya Wi-Fi ile.

## Kararlar

| Konu | Karar | Neden |
|---|---|---|
| Lisans | GPL-3.0 | Moonlight/Sunshine gibi GPL projelerden kod ve eski cihaz düzeltmeleri alabilmek; türevlerin açık kalması |
| Windows tarafı | C++ (servis, yakalama, kodlama, sürücü) | IddCx sürücüsü zaten C++; DXGI/NVENC/AMF örneklerinin tamamı C/C++ |
| Android istemci | Java, minSdk 16 (4.1), AndroidX yok | MediaCodec 4.1'de geldi; güncel AndroidX/NDK 4.x'i desteklemiyor |
| iOS istemci | Swift, VideoToolbox | Yerel uygulama isteniyor; çok eski iOS sürümleri web istemcisiyle |
| Web istemcisi | Framework'süz JavaScript | Eski tarayıcılar |
| Hedef PC'ler | Windows 10 1809+ / 11; ekransız mini PC ve masaüstü | "Tek ekran" senaryosu birinci sınıf vatandaş |
| Sanal ekran | Başta [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver) (MIT, imzalı), sonra kendi IddCx sürücümüz | Erken aşamada sürücü imzası derdi olmadan ilerlemek |
| Şifreleme | Noise protokolü + eşleştirme kodu | Android 4.x'in TLS yığını eski; ChaCha20 donanım AES'i olmayan işlemcilerde hızlı |

## Bilinen sınırlar

- BIOS, açılış, Güvenli Mod ve BitLocker PIN ekranı yazılımla gösterilemez (bkz. deneysel HDMI modu).
- Android 4.x'te `adb reverse` yok: bağlantıyı her zaman PC başlatır (`adb forward`).
- Android 4.4 öncesinde gezinme çubuğu kalıcı gizlenemez: sanal monitör tabletin kullanılabilir alanı
  kadar açılır (S6000'de 1280×752).
- DRM'li içerik sanal ekranda siyah görünebilir.

## Faz 0 – Duman testi ✅

Hedef: eski tabletin donanım çözücüsünü ölçmek ve tasarımı gerçek veriye dayandırmak.

Test cihazı: Lenovo IdeaTab S6000, Android 4.2.2, MediaTek MT8389, `OMX.MTK.VIDEO.DECODER.AVC`.
PC: RTX 2060 (NVENC). Akış: 1280×752 H.264 Constrained Baseline, B-kare yok.

| Bağlantı | fps | Çözücü gecikmesi (ort) | Düşen kare |
|---|---|---|---|
| USB (adb forward) | 30 | ~107 ms | 0 |
| USB (adb forward) | 60 | ~58 ms | 0 |
| USB (adb forward) | 90 | ~41 ms | 0 |
| USB (adb forward) | 120 | ~33 ms | 0 |
| Wi-Fi 2.4 GHz | 60 | ~58 ms | 0 |
| Wi-Fi 2.4 GHz | 120 | ~35 ms | 0 |

Bulgular:

1. MTK çözücü fps ne olursa olsun **içinde sabit ~3 kare tutuyor**. Gecikme kare sayısıyla ölçekleniyor,
   çözme süresiyle değil.
2. İşe yaramayanlar: SPS'e `max_dec_frame_buffering=1` / `max_num_reorder_frames=0` yazmak (çözücü
   `MaxDPBNumber = 16` kullanıyor), satıcıya özel düşük gecikme anahtarları, çıkışı yüzeye çizmemek,
   karelerin arasına boş AUD paketi koymak.
3. İşe yarayan: **kare aralığını kısaltmak.** Tablet 1280×752'yi 120 fps'te kare düşürmeden çözüyor.
   Değişmeyen bölgeler neredeyse sıfır baytlık "atla" bloklarıyla kodlandığı için yük düşük kalıyor.
   → Faz 1 kodlayıcısında: ekran değişince arkasından birkaç hızlı "itici" kare, ekran sabitken hiç kare yok.
4. Tabletin Wi-Fi'si 2.4 GHz, boşta ping ~80–115 ms (PC→modem 10 ms). Yüksek performans Wi-Fi kilidi
   biraz iyileştiriyor. Gerçek ağ gecikmesi Faz 1 protokolündeki ping/pong ile ölçülecek; bu cihaz
   için USB öncelikli.
5. Tablette `f_accessory` (AOA) ve `f_rndis` (USB internet paylaşımı) çekirdek desteği var: iki USB yolu da açık.

Deneme: `tools/faz0.ps1` (PC'den gönderir), `tools/sayac.html` (uçtan uca gecikmeyi fotoğrafla ölçmek için).

## Faz 1 – İlk sürüm: yansıtma ✅ (0.1.0)

Tasarım kuralı: **sistemi az yor.** Görüntü CPU'ya hiç inmez (DXGI → GPU video işlemci → donanım
kodlayıcı), ekran değişmezken hiçbir şey kodlanmaz/gönderilmez, imleç tablette yerel çizilir.
Derleme için Visual Studio gerekmez: MinGW-w64 (GCC) yeterli.

- [x] Protokol v1: uzunluk önekli kareler, el sıkışma, kare onayı, ping/pong → [PROTOCOL.md](PROTOCOL.md)
- [x] Windows sunucu (C++, tek 350 KB exe): DXGI yakalama → GPU'da ölçek + NV12 → Media Foundation
      donanım kodlayıcı (NVIDIA/AMD/Intel tek kod yolu), düşük gecikmeli VBR
- [x] İtici kareler (Faz 0 bulgusu 3) + onay tabanlı akış kontrolü (gecikme birikmez)
- [x] İmleç ayrı gönderilir, tablette çizilir (imleç hareketi kodlama gerektirmez)
- [x] USB-ADB: adb forward, tableti uyandırma, uygulamayı başlatma; Wi-Fi: `--ip`
- [x] Dokunma → fare (tek parmak)
- [x] Wi-Fi'de otomatik bulma (her ağ arayüzüne UDP yayını; USB internet paylaşımı ağı da taranır — gerçek cihazda henüz denenmedi)
- [x] Otomatik mod: önce USB, yoksa Wi-Fi; kopunca yeniden bağlanma (USB'de tablet uygulamasını da yeniden başlatır)
- [x] Eşleştirme + şifreleme: geçici ECDH P-256; Wi-Fi'de ilk eşleştirmede iki tarafta aynı 6 haneli
      kod onayı, USB'de onaysız; Wi-Fi verisi ChaCha20-Poly1305 (RFC 8439 test vektörleriyle doğrulandı,
      `ekran-test.exe`), USB şifresiz. Ayrıntı: [PROTOCOL.md](PROTOCOL.md)
- [x] PC'deki eşleştirme sorusu, tablet reddedince kendiliğinden kapanır
- [x] Tablette "Eşleşmeleri unut" düğmesi (yalnızca bağlı değilken görünür)
- [x] Bildirim alanı uygulaması `ekran.exe`: durum, bağlantı türü, ekran seçimi, Windows ile başlat, günlük
- [x] USB'de otomatik kurulum/güncelleme: uygulama yoksa ya da paketteki sürümden eskiyse adb ile kurulur
- [x] Wi-Fi'den kurulum: menüden 10 dakikalık indirme sayfası (Android 4.2 tarayıcısında denendi).
      QR kod yok: eski tabletlerin çoğunda QR okuyucu bulunmuyor
- [x] Taşınabilir paket: `tools\paketle.ps1` → `dist\EKRAN-<sürüm>-windows.zip` (exe'ler + APK + adb, 3,6 MB)
- [ ] Kalıcı APK sürüm anahtarı (GitHub sürümleri için; şu an hata ayıklama anahtarı)
- [ ] USB yoklamasını `adb get-state` yerine Windows cihaz bildirimleriyle yapma
- [ ] Yazılım kodlayıcı yedeği (donanım kodlayıcısı olmayan PC'ler)

Ölçüm (S6000, USB, RTX 2060):

| Durum | PC→tablette çizim | PC CPU (tek çekirdek) |
|---|---|---|
| Sürekli hareket (~116 fps) | ~21 ms | ~%10 |
| Seyrek değişim (~10/s) | ~38 ms (24 ms'si itici kareler) | ~%2 |
| Ekran sabit | — (kare yok) | ~%0 |
| Tablet yok, aranıyor | — | %0,000 (9,7 MB bellek) |

Şifreleme seçimi (S6000, Android 4.2, tek çekirdek, 16 KB'lık parçalarla):

| Algoritma | Sağlayıcı | Hız |
|---|---|---|
| AES-128-CTR | BouncyCastle (saf Java) | 1,4 MB/s |
| HMAC-SHA256 | BouncyCastle (saf Java) | 1,7 MB/s |
| ChaCha20 | EKRAN (Dalvik için elle açılmış) | 15,4 MB/s |
| Poly1305 | EKRAN | 18,1 MB/s |

Sürekli harekette (116 kare/s, 6,6 Mbit/s) tablet uygulamasının CPU'su: USB %33, Wi-Fi AES+HMAC %104,
Wi-Fi ChaCha20-Poly1305 %48. Not: Dalvik JIT küçük metotları (ör. `Integer.rotateLeft`) satır içine
almıyor; sıcak döngülerde elle açmak ~7 kat hız kazandırdı.
- [ ] Tablet tarafında kare başına kopyaları ve onay mesajlarını azaltmak (USB'de %33'ü düşürmek)
## Faz 2 – İkinci ekran (sürüyor)

Sanal monitör için hazır sürücüler değerlendirildi: Virtual Display Driver (MIT, imzalı ama monitör
eklemek için tüm sürücüyü yeniden yükler), Parsec VDD (kapalı kaynak, dağıtılamaz), SudoVDA (MIT, tablet
başına ekle/çıkar API'si var ama imzalı sürümü yok), HDMI sahte takı (sürücüsüz; `--ekran N` ile bugün de
çalışır). Seçim: **SudoVDA tabanlı kendi sürücümüz** → [driver/](../driver/README.md), bu bilgisayarda
MSVC + WDK ile derlenip yerel sertifikayla imzalanıyor.

- [x] EKRAN sanal ekran sürücüsü (IddCx/UMDF): tablet başına tak-çıkar, sabit monitör kimliği
      (Windows yerleşimi hatırlar), EDID'de fiziksel boyut, 3 sn bekçi köpeği (program çökse de monitör kalkar)
- [x] Kurulum/kaldırma: `driver\kur.ps1` (özel anahtarı imzadan hemen sonra silen yerel imza), `driver\kaldir.ps1`
- [x] PC programı: tablet bağlanınca 1280×752 monitör aç → onu yakala; ayrılınca kaldır; sürücü yoksa yansıt
- [x] Tepsi menüsü: Görüntü → İkinci ekran / Yansıtma
- [x] Çoklu dokunma: Windows'a gerçek dokunmatik ekran olarak (InjectTouchInput, 10 parmak; kaydırma, yakınlaştırma,
      basılı tutunca sağ tık, dokunmatik klavye). Tepsi menüsünden "Fare" moduna geçilebilir. Basınçlı kalem kapsam dışı.
- [x] Donanım imleci: imleç tablette yerel çizilir (Faz 1'de yapıldı)
- [x] Döndürme: tablet dik tutulunca sanal monitör Windows'ta dikey olur (DMDO_90/270). Tabletin ekranı,
      akış boyutu ve çözücü hiç değişmez — Windows döndürülmüş görüntüyü zaten panel yönünde verir; yalnızca
      imleç ve dokunma çevrilir. Tabletin "otomatik döndür" kilidine uyulur; ivmeölçer yalnızca bağlıyken ve
      en yavaş hızda (200 ms) açılır. Yön/çözünürlük/yerleşim değişiminde bağlantı artık kopmaz.
- [x] Tablet bilgi katmanı yenilendi (karşılama ekranı, ölçüm kartı, eşleştirme onayı) ve tabletin tutuluşuna göre
      döner; Android iletişim kutuları çevrilemediği için eşleştirme ve "eşleşmeleri unut" onayı da bu katmanda
- [x] Yansıtmada dikey çevrilmiş PC monitörü video işlemcide düz çevrilir (desteklemeyen kartta yan kalır;
      dokunma ve imleç her iki durumda doğru)
- [ ] Görüntüyü DXGI yakalama yerine doğrudan sürücüden almak (bir kopya daha az, oturum açma ekranı)
- [ ] Dağıtım imzası (SignPath Foundation veya EV + Microsoft) — şu an sürücü yalnızca derlendiği bilgisayarda kurulabiliyor

Ölçüm (S6000, USB, ikinci ekran 1280×752): PC→tablette çizim ~27 ms, ekran sabitken PC CPU %0,3.
### Kaynak kullanımı optimizasyonu

Ölçüm: S6000 + RTX 2060, USB, yansıtma, ekranda sürekli hareket (sayaç sayfası). PC RAM = Görev
Yöneticisi'ndeki "özel çalışma kümesi"; tablet RAM = `dumpsys meminfo` PSS.

| | Önce | Sonra (dengeli) |
|---|---|---|
| PC işlemci (tek çekirdek) | %14,4 | %4,3 |
| PC RAM | 25 MB | 25 MB |
| Tablet uygulaması | %39,4 | %19,2 |
| Tablette donanım çözücü (mediaserver) | %15,0 | %10,6 |
| Tablet RAM | 28 MB | 26 MB |
| Hareket sırasında gecikme | ~20 ms | ~36 ms |
| Ekran sabitken (ikinci ekran) | — | PC %0,6 · tablet %2 + %0,9 |

Yapılanlar:
- Kare hızı sınırı 60 (tabletin ekranı): PC ekranı 120/144 Hz olsa bile gösterilemeyecek kare kodlanmaz/çözülmez.
- İtici kareler "dengeli" modda yalnızca hareket durunca; "düşük gecikme" modunda (tepsi → Performans)
  kareler arasına da girer: ~32 ms, tablet ~1,5 kat çalışır.
- Tablet: P-karelerinde kısa yol (tarama ve ara kopya yok), tamponlu gönderim, toplu kare onayı
  (3 karede bir; çözücü boşalınca hemen), çözücü çıkış döngüsü boşta daha seyrek uyanır, gizli ölçüm
  katmanı hiçbir iş yapmaz.
- PC: D3D11 sürücü iş parçacığı optimizasyonu kapalı (iki iş parçacığı az).
- PC'deki "özel bellek" (~93 MB) büyük ölçüde NVIDIA sürücüsünün ayırıp kullanmadığı sanal alan; gerçek
  RAM ~25 MB (D3D11 cihazı ~60 MB sanal, Media Foundation kodlayıcı ~32 MB sanal). Tanı: `EKRAN_BELLEK=1`.
## Faz 3 – Gerçek harici ekran

- [ ] AOA ile tak-çalıştır (tablette ayar yok, uygulama kendiliğinden açılır)
- [ ] Kendi IddCx sürücümüz: cihaz başına tak-çıkar, cihaza özel EDID (Windows yerleşimi hatırlar),
      fiziksel boyut → doğru DPI
- [x] Windows hizmeti (tepsi → "Oturum açılmadan önce başlat"): açılışta, oturum açılmadan başlar. Hizmet
      (Oturum 0) ekrandaki oturumda SYSTEM yetkili bir ajan başlatır; ajan giriş masaüstünü (Default ↔
      Winlogon) izleyerek kilit ekranını, şifre ekranını ve Ctrl+Alt+Del'i yakalar, dokunmayı oraya iletir.
      Ayarlar/durum HKLM'de (tepsi yalnızca arayüz), eşleşmeler yalnızca SYSTEM'in okuyabildiği bölümde;
      kilitliyken yeni tablet eşleştirilmez. Kullanıcının adb anahtarı taşınır (tablet yeniden izin sormaz).
      Ölçüldü (S6000, USB, tablet ana ekran): kilit ekranı tablette görünür, yukarı kaydırınca şifre ekranı
      açılır, dokunma iletilir. Not: Windows kilit ekranını yalnızca **ana** monitörde çizer (ekransız mini
      PC'de tablet zaten ana ekran; masaüstünde "Tek ekran" ya da tableti ana ekran yapmak gerekir).
      UAC güvenli masaüstü aynı yoldan geçer ama bu PC'de UAC sormadan yükselttiği için denenemedi
- [x] Otomatik bağlanma iki tarafta ayarlanır: PC (tepsi → Bağlantı → Otomatik bağlan / Şimdi bağlan) ve
      tablet (karşılama ekranı → Bilgisayara takılınca otomatik aç); ikisi açıksa takınca kendiliğinden olur
- [x] Arka plan: tablet uygulaması alta alınınca bağlantı ve Windows'taki monitör sürer (ön plan hizmeti +
      bildirim); arkadayken PC görüntü üretmez (ajan %0 işlemci), öne gelince anahtar kareyle anında döner.
      Bağlantıyı kesmek: bildirim ya da ölçüm kartındaki "Bağlantıyı kes"
- [x] USB takılma bildirimi (CM_Register_Notification, ADB arabirimi): tablet takılı değilken adb hiç
      çalıştırılmaz; kurulum denetimi aygıt başına bir kez
- [ ] Oturum açılmadan Wi-Fi: tablet uygulaması açık ve eşleşmiş olmalı (bugün de öyle); ilk açılışta
      tabletin bağlanması için USB önerilir
- [x] Tek ekran modu (tepsi → Görüntü → Tek ekran, `--tek-ekran`): tablet bağlanınca diğer monitörler kapanır,
      tablet ana ekran olur; ayrılınca eski düzen geri yüklenir. Düzen Windows'un veritabanına kaydedilmez:
      program çökerse sürücünün bekçi köpeği monitörü kaldırır, Windows kayıtlı (tabletsiz) düzene döner
- [x] Uyku eşitleme: Windows ekranları kapatınca (güç ayarı bildirimi, yoklama yok) tablet kararır ve kendi
      zaman aşımıyla ekranını kapatır; bağlantı ve Wi-Fi ayakta kalır. Windows ekranı açınca tablet de yanar.
      USB'de PC tablet ekranını adb ile hemen kapatır (geliştirici seçeneklerindeki "uyanık kal" açıkken
      zaman aşımı hiç işlemez). Kararıkken tablete dokunmak Windows'u uyandırır.
      Ölçüldü (S6000, USB): tablet ekranı ~3 sn'de kapanır, fare oynatınca ~4 sn'de görüntü döner, bağlantı kopmaz
- [ ] Çoklu tablet

## Faz 4 – Her cihaz

- [ ] iOS uygulaması (Swift, VideoToolbox)
- [ ] Web istemcisi (JPEG modu eski tarayıcılar, H.264 modu yeni tarayıcılar)
- [ ] Hafif mod: sadece değişen bölgeler JPEG olarak
- [ ] Ses aktarımı
- [ ] E-mürekkep (e-ink) modu

## Faz 5 – Ekstralar

- [ ] Sistem paneli (CPU/GPU sıcaklığı, saat, medya) – video akışı olmadan, yerel çizim
- [ ] Kısayol paneli (Stream Deck benzeri)
- [ ] Pil güvenliği: sürekli şarjda sıcaklık/şarj izleme ve uyarı
- [ ] Video duvarı, pano paylaşımı, touchpad modu
- [ ] Deneysel HDMI modu: USB HDMI yakalama çubuğu + OTG → BIOS dahil gerçek monitör
- [ ] Linux / macOS sunucu
