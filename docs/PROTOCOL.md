# EkranLink protokolü v2

Tek TCP bağlantısı, varsayılan port **27183**. Bağlantıyı her zaman PC açar (Android 4.x'te
`adb reverse` yok): USB'de `adb forward tcp:27183 tcp:27183`, Wi-Fi'de tabletin IP'si.

Tüm sayılar aksi belirtilmedikçe **big-endian**. `str` = `u16 uzunluk` + UTF-8 bayt.

## Bulunma (UDP, port 27183)

PC, her ağ arayüzünün yayın adresine (ör. 192.168.1.255, USB internet paylaşımında 192.168.42.255) sorar:

- Soru (P→T): `"EKRN?"` + `u16 sürüm`
- Cevap (T→P, soranın adresine): `"EKRN!"` + `u16 sürüm, u16 tcpPort, str ad, str kimlik`

Tablet boştayken hiçbir paket göndermez; yalnızca soru gelince cevap verir.

Soru her arayüzden, o arayüzün adresine bağlı ayrı bir soketle gönderilir: aynı ağa iki kartla
(Ethernet + Wi-Fi) bağlı PC'de bağlanmamış soketin yayını yalnızca yönlendirmenin seçtiği karttan çıkar.
Wi-Fi yayını onaysız gider ve güç tasarrufundaki tablet kaçırabilir; bu yüzden soru 1 sn içinde 3 kez
yinelenir ve daha önce Wi-Fi ile bağlanılan tabletlerin adreslerine (en çok 4, eşleşme deposunda
`Tabletler`) doğrudan da sorulur. Doğrudan paketi erişim noktası onaylı ve yeniden göndererek iletir.

## Çerçeve

```
u32 gövdeUzunluğu
gövde = u8 tür + yük
[16 bayt etiket]      yalnızca şifreli bağlantıda
```

Şifreli bağlantıda gövde **ChaCha20-Poly1305** (RFC 8439, ek veri boş) ile yerinde şifrelenir.
Nonce = `00 00 00 00` + `sıra no (u64, little-endian)`; her yönün kendi anahtarı ve 0'dan başlayan
sıra numarası vardır, sıra no her çerçevede artar (tekrar gönderme / sıra değiştirme işe yaramaz).
Etiket tutmazsa bağlantı hemen kesilir.

## Açılış ve güvenlik

```
P→T  "EKRN" + u16 sürüm (2)                                              ham
P→T  PcHello   u8 bayrak, 16 pcKimlik, 65 ecdhP, 16 nonceP, str pcAdı    şifresiz çerçeve
T→P  TabletKey 65 ecdhT, 16 nonceT, str cihazKimliği, str cihazAdı, u8 buPC'yiTanıyorum
P→T  Mode      u8 eşleşmiş, u8 şifreli
     ── bu noktadan sonra (şifreliyse) tüm çerçeveler şifreli ──
     (eşleşmemişse) P→T ve T→P PairResult u8 onay
T→P  Hello ...
```

Tablet PC'nin sürümünü desteklemiyorsa `TabletKey` yerine `Incompatible` (24) gönderir:
`u16 tabletinSürümü, str uygulamaSürümü` (şifresiz çerçeve) ve bağlantıyı kapatır. İki taraf da
kullanıcıya hangi uygulamanın güncelleneceğini söyler (PC tablete uygulama kurmaz; ikisi ayrı indirilir).

- `ecdh*`: geçici ECDH P-256 açık anahtarı (`04 || X || Y`), her bağlantıda yenisi üretilir.
  Tablet gelen noktanın eğri üzerinde olduğunu doğrular; Windows CNG içe aktarırken doğrular.
- `Z` = ECDH ortak sırrı (X koordinatı, 32 bayt, big-endian).
- `H` = SHA-256(ecdhP ‖ ecdhT ‖ nonceP ‖ nonceT ‖ pcKimlik ‖ cihazKimliği)
- `K` = eşleşme anahtarı (eşleşmişse), değilse 32 sıfır bayt
- `PRK` = HMAC-SHA256(anahtar = H, veri = Z ‖ K)
- Oturum anahtarları: `HMAC(PRK, "ekran p2t key")` (PC→tablet), `HMAC(PRK, "ekran t2p key")` (tablet→PC)

**Eşleştirme** (`eşleşmiş = 0`): iki taraf da `HMAC(PRK, "ekran sas")`'ın ilk 4 baytından
(big-endian, mod 1 000 000) 6 haneli kodu gösterir. Araya giren biri iki tarafla ayrı anahtar
değişimi yaparsa kodlar farklı çıkar ve kullanıcı fark eder. Kod oturuma bağlı olduğundan kaba
kuvvetle denenemez. İki taraf da onaylarsa yeni eşleşme anahtarı `HMAC(PRK, "ekran pair")` saklanır.
Tablette "Reddet" bağlantıyı kapatır; PC bunu da ret sayar ve o tableti 1 dakika tekrar sormaz.

**USB** (adb forward; tablette bağlantı 127.0.0.1'den gelir): kablo + adb yetkisi güvenilir kabul
edilir; eşleştirme onaysız yapılır ve tabletin işlemcisini yormamak için `şifreli = 0` seçilir.
Tablet, yerel olmayan bir bağlantıdan gelen `şifreli = 0` isteğini reddeder.

Eşleşmiş bir cihazın anahtarı tutmazsa `Hello` doğrulanamaz; PC kendi kaydını siler ve bir
sonraki bağlantıda yeniden eşleştirme ister.

## Mesajlar

| Tür | Ad | Yön | Yük |
|---|---|---|---|
| 1 | Hello | T→P | `u16 sürüm, u16 yüzeyGen, u16 yüzeyYük, u16 ekranGen, u16 ekranYük, u16 dpi, u16 sdk, str model, str çözücü` |
| 2 | Config | P→T | `u16 akışGen, u16 akışYük, u8 codec(1=H.264), u16 kaynakGen, u16 kaynakYük, u16 içerikX, u16 içerikY, u16 içerikGen, u16 içerikYük` |
| 3 | Video | P→T | `u64 pts(µs, PC saati), u8 bayrak(bit0 anahtar kare, bit1 itici kare), ... tek karenin Annex-B verisi` |
| 4 | Ping | P→T | `u64 zaman` |
| 5 | Pong | T→P | `u64 zaman` (Ping'deki değer aynen) |
| 6 | FrameAck | T→P | `u64 pts, u32 çözmeSüresi(µs), u16 adet` — son çizilen kare; `adet` bu onayın kapsadığı kare sayısı (hareket sürerken 3'te bir, çözücü boşalınca hemen gönderilir; yoksa 1) |
| 7 | CursorShape | P→T | `u16 gen, u16 yük, u16 sıcakX, u16 sıcakY, gen×yük adet u32 ARGB` |
| 8 | CursorPos | P→T | `s16 x, s16 y, u8 görünür` — imlecin sol üstü, kaynak görüntü pikseli |
| 10 | Touch | T→P | `u8 eylem(0 bas,1 kaydır,2 bırak,3 iptal), u8 parmak, u16 x, u16 y, u16 basınç(0–1024)` — akış pikseli; her parmak ayrı `parmak` kimliğiyle, `iptal` tüm parmakları iptal eder |
| 11 | Orientation | T→P | `u16 derece` (0, 90, 270) — tabletin tutuluş yönü, yatay ekrana göre saat yönünde. Yalnızca tabletin otomatik döndürmesi açıkken, bağlanınca ve yön değişince gönderilir |
| 12 | DisplayPower | P→T | `u8 açık` — Windows ekranları kapattı (0) / açtı (1). Tablet kararır ve ekranının kendi zaman aşımıyla kapanmasına izin verir; bağlantı sürer. Açılınca tablet ekranını yakar |
| 13 | KeyRequest | T→P | (boş) — tablet çözücüsünü yeniden kurdu (yüzeyi yok olup geri geldi); PC anahtar kare gönderir ve onay sayacını sıfırlar |
| 14 | VideoVisible | T→P | `u8 önde` — tablet uygulaması arka plana alındı (0) / öne geldi (1). Arkadayken PC yakalama ve kodlamayı durdurur, bağlantı sürer; öne gelince tablet `KeyRequest` gönderir. Arkadayken PC tablet ekranını kapatmaz |
| 20–24 | PcHello, TabletKey, Mode, PairResult, Incompatible | | yukarıda |

Tablet ilk 4 bayt `"EKRN"` değilse akışı ham H.264 Annex-B kabul eder (tanı modu, `tools/faz0.ps1`).

### İçerik dikdörtgeni

Kaynak ekran (ör. 1920×1080) oranı korunarak akışa (ör. 1280×752) yerleştirilir; kalan kısım siyah.
`içerik*` bu yerleşimi akış pikseli cinsinden verir. Tablet imleci buna göre konumlar/ölçekler,
PC dokunmayı bunun tersine göre masaüstüne çevirir.

### USB'de otomatik açılma

PC, adb ile tablete sorar: `am broadcast -f 32 -n org.ekranlink.android/.AutoStartReceiver`. Sonuç kodu
tabletin kararıdır: **1** izin var (PC uygulamayı `am start` ile başlatır; Android 10+ arka plandan ekran
açmaya izin vermediği için başlatan PC'dir), **2** reddedildi ("bilgisayara takılınca otomatik aç" kapalı ya
da kullanıcı bağlantıyı tabletten kesti — kablo 30 sn çıkarılana ya da uygulama elle açılana kadar),
**3** uygulama zaten açık (PC bağlanır). PC'de de "otomatik bağlan" ayarı vardır: ikisi de açıksa tablet
takılınca kendiliğinden ekran olur.

Sonuç verisi `kimlik|ad` (ör. `9774d56d682e549c|LENOVO Vodafone Smart Tab III 10`): kimlik, el sıkışmada ve
Wi-Fi keşfinde gönderilenin aynısıdır. PC cihaz bulurken uygulamayı açmadan da sorar (`--ez kimlik true`; bu
soru "bağlantı kesildi" bekletmesini uzatmaz): birden çok cihazı ayırt eder, aynı cihazı USB'de ve Wi-Fi'de
iki kez listelemez (ikisinde de varsa USB, biri bağlıysa o).

### Birden çok cihaz

Her cihaz ayrı bir oturumdur (ayrı TCP bağlantısı, kodlayıcı ve ikinci ekranda ayrı sanal monitör). USB'de
her cihaz için PC'de ayrı bir yerel port açılır (`adb -s SERİ forward tcp:0 tcp:27183`; adb boş bir port
seçer). Tek ekran modunda EkranLink'in tüm monitörleri açık kalır, yalnızca diğerleri kapanır.

### Döndürme

Tablet uygulaması hep yatay kalır. Tablet dik tutulunca `Orientation` gönderir; PC ikinci ekran modunda sanal
monitörü döndürür (90 → DMDO_90, 270 → DMDO_270). Windows döndürülmüş ekranın görüntüsünü panelin kendi
yönünde verdiğinden akış, `Config` ve çözücü değişmez: `kaynakGen×kaynakYük` her zaman bu görüntünün
boyutudur. PC imleç resmini ve konumunu bu görüntünün yönüne çevirerek gönderir, gelen dokunmayı da tersine
çevirerek masaüstüne uygular. Yansıtma modunda PC'nin gerçek monitörü döndürülmez.

### İtici kareler ve akış kontrolü

Bazı donanım çözücüler (ör. MediaTek) içinde sabit ~3 kare tutar; yeni kare gelmezse son görüntü
ekrana hiç çıkmaz. Bu yüzden PC, ekran değiştikten sonra kısa aralıklarla birkaç **itici kare**
(değişmemiş görüntü, neredeyse sıfır bayt) gönderir. Ekran sabitken hiçbir kare gönderilmez.
Gerçek karelerin pts'i çift, itici karelerinki tek sayıdır.

PC, gönderdiği ama `FrameAck` almadığı kare sayısını sınırlar. Sınıra ulaşınca yeni kare kodlamaz,
yalnızca "ekran değişti" bilgisini tutar; onay gelince en güncel görüntüyü kodlar. Böylece yavaş
bağlantıda gecikme birikmez.
