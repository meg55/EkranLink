# EkranLink sanal ekran sürücüsü

Tablet bağlanınca Windows'a tabletin kendi çözünürlüğünde gerçek bir monitör ekleyen, ayrılınca
kaldıran IddCx (Indirect Display) sürücüsü. Kullanıcı modunda (UMDF 2) çalışır; çekirdeğe kod yüklemez.

## Kaynak ve lisans

Bu klasör **EkranLink'in geri kalanından ayrı lisanslıdır.**

- [SudoVDA](https://github.com/SudoMaker/SudoVDA) (SudoMaker) tabanlıdır — lisansı: *"MIT and CC0 or
  Public Domain … choose the least restrictive option"*.
- SudoVDA, Microsoft'un [Indirect Display Driver örneğine](https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay)
  dayanır — lisansı: MS-PL.
- EkranLink değişiklikleri (MIT): EkranLink'e özel donanım kimliği / arayüz GUID'i / kayıt defteri yolu
  (SudoVDA veya Apollo kurulu makinelerde çakışmasın), dış girdiler için sınır denetimleri ve
  sonlandırılmış dizgiler (EDID'e kopyalanan 14 baytlık alanlar), tabletin fiziksel boyutunun EDID'e
  yazılması (Windows ölçeklemeyi buna göre önerir), "EKR" üretici kodu, gereksiz yüksek süreç önceliğinin
  kaldırılması, eksik `break`'ler, imleç olayı sızıntısı, aygıt erişiminin oturum açmış kullanıcılarla
  sınırlanması (`IU`).

## Komutlar (`src/ekranvd-ioctl.h`)

| Komut | Ne yapar |
|---|---|
| `IOCTL_ADD_VIRTUAL_DISPLAY` | Genişlik, yükseklik, tazeleme, monitör GUID'i, ad, seri no, fiziksel boyut (mm) → monitör ekler |
| `IOCTL_REMOVE_VIRTUAL_DISPLAY` | GUID ile monitörü kaldırır |
| `IOCTL_DRIVER_PING` | Bekçi köpeğini besler. PC programı 3 sn ses vermezse tüm sanal monitörler kaldırılır (program çökerse "görünmeyen ekran" kalmaz) |

Aynı tablet her bağlanışında aynı monitör GUID'ini alır (cihaz kimliğinden türetilir), böylece Windows
her tabletin ekran yerleşimini ve ölçeğini ayrı hatırlar.

## Derleme, kurulum, kaldırma

Gerekenler: Visual Studio Build Tools (MSVC x64) + WDK 10.0.26100. MSBuild'in sürücü araç setine gerek yok.

```powershell
.\build.ps1      # out\EkranLinkVD.dll + ekranlinkvd.inf
.\kur.ps1        # yönetici: imzala + güven + kök aygıtı oluştur (host-windows\build\ekranlink-surucu.exe gerekir)
.\kaldir.ps1     # yönetici: aygıt, sürücü paketi, sertifika ve ayarları tamamen kaldırır
```

**İmza:** UMDF sürücüsü olduğu için Windows test modu gerekmez. `kur.ps1` her seferinde yeni bir kod
imzalama sertifikası üretir, sürücüyü imzalar, sertifikanın yalnızca açık kısmını "Güvenilen Kök" ve
"Güvenilen Yayımcılar" deposuna ekler ve **özel anahtarı hemen siler** — bu sertifikayla başka hiçbir şey
imzalanamaz. Bu yöntem yalnızca derlemenin yapıldığı bilgisayar için uygundur; dağıtım için SignPath
Foundation (ücretsiz, açık kaynak) ya da EV sertifika + Microsoft imzası gerekir.
