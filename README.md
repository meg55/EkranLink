# EKRAN

Eski tablet ve telefonlarınızı bilgisayarınız için ekran yapın: **yansıtma**, **ikinci ekran** veya
**Windows'un tek ekranı** olarak, **USB** ya da **Wi-Fi** ile. Android 4.1'e kadar eski cihazları destekler.

*Turn old tablets and phones into displays for your PC — mirror, extend, or use as the only screen,
over USB or Wi-Fi. Supports devices as old as Android 4.1.*

> **Durum:** yansıtma ve **ikinci ekran** (USB ve şifreli Wi-Fi), dokunmatik fare, otomatik bulma ve kurulum çalışıyor.
> İkinci ekran için sanal ekran sürücüsü şimdilik yalnızca derlendiği bilgisayarda kurulabiliyor ([driver/](driver/README.md)).
> Plan ve ölçümler: [docs/ROADMAP.md](docs/ROADMAP.md) · Test edilen cihazlar: [docs/DEVICES.md](docs/DEVICES.md)

## Neden?

- Mevcut çözümlerin çoğu kapalı kaynak ve Android 5.0+ istiyor; çekmecedeki 4.x tabletler dışarıda kalıyor.
- Hedef, "eski tableti tak, harici monitör olsun" deneyimi: USB'yi takınca Windows'ta yeni bir monitör belirir.

## Depo yapısı

```
android/        Android istemci (Java, minSdk 16)
host-windows/   Windows sunucusu (C++, MinGW ile derlenir)
driver/         Sanal ekran sürücüsü (IddCx, SudoVDA tabanlı, MSVC + WDK ile derlenir; ayrı lisans)
docs/           Yol haritası, protokol, cihaz listesi
tools/          Geliştirme ve ölçüm betikleri
```

## Kullanım

1. `dist\EKRAN-<sürüm>-windows.zip` dosyasını açın, `ekran.exe`'yi çalıştırın (kurulum gerekmez).
   Saatin yanında bir monitör simgesi belirir.
2. Tablete uygulamayı kurun:
   - **USB:** Tablette *Geliştirici seçenekleri > USB hata ayıklama* açıksa tableti takmanız yeterli;
     EKRAN uygulamayı kendisi kurar ve açar.
   - **Wi-Fi:** Simge > *Tablete uygulamayı kur…*, çıkan adresi tabletin tarayıcısında açın.
3. Tablette EKRAN açıkken bilgisayar tableti kendiliğinden bulur (önce USB, yoksa Wi-Fi).
   Wi-Fi ile ilk bağlantıda iki cihazda aynı 6 haneli kod görünür; ikisinde de onaylayın.

Tablet Windows'a çoklu dokunmatik ekran olarak bağlanır: kaydırma, iki parmakla yakınlaştırma, basılı tutunca sağ tık.
Üç parmakla dokunmak ölçümleri gösterir/gizler.

**Ekransız mini PC:** simge > *Oturum açılmadan önce başlat* (bir kez yönetici izni ister). EKRAN Windows
hizmeti olarak açılışta başlar; kilit ekranı ve şifre ekranı tablette görünür, tabletten açılır (kilit
perdesini yukarı kaydırın). Windows kilit ekranını yalnızca ana monitörde gösterir.

**Otomatik bağlanma:** PC'de simge > *Bağlantı* > *Otomatik bağlan*, tablette karşılama ekranındaki
*Bilgisayara takılınca otomatik aç*. İkisi de açıksa tableti takmanız yeterli; biri kapalıysa PC'de
*Şimdi bağlan*'a tıklayın ya da tablette EKRAN'ı açın.

**Arka plan:** Tabletteki uygulamayı alta alıp başka uygulama kullanabilirsiniz; Windows'taki monitör
kopmaz, dönünce görüntü kaldığı yerden sürer. Bağlantıyı kesmek için bildirimdeki ya da ölçüm kartındaki
(üç parmak) *Bağlantıyı kes*.

Görüntü modları (simge > *Görüntü*): **İkinci ekran**, **Tek ekran** (tablet bağlıyken diğer monitörler
kapanır, tablet ayrılınca geri gelir) ve **Yansıtma**. Windows ekranı kapatınca tablet de kararır.

İkinci ekran modunda tableti dik çevirmek Windows'taki monitörü de dikey yapar (tablette *Otomatik döndür*
açık olmalı; kapalıysa monitör yatay kalır).

## Derlemek

Gerekenler: JDK 17+ ve Android SDK (Android Studio ile gelir), MinGW-w64 GCC + CMake + Ninja
(`winget install BrechtSanders.WinLibs.POSIX.MSVCRT`). Visual Studio gerekmez.

```powershell
.\tools\paketle.ps1                          # her şeyi derler, testleri çalıştırır, zip üretir
.\host-windows\build.ps1                     # yalnızca Windows tarafı
.\host-windows\build\ekran-host.exe          # konsol sürümü, saniyelik ölçümlerle (--usb / --wifi / --ip / --apk-sun)
.\host-windows\build\ekran-test.exe          # şifreleme test vektörleri (RFC 8439)
```

Tanı için ham akış testi: `.\tools\faz0.ps1` (ffmpeg gerekir).
## Lisans

[GPL-3.0](LICENSE)
