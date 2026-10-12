# Contributing / Katkı

**English** · [Türkçe](#türkçe)

Issues and pull requests are welcome in English or Turkish.

## Reporting a bug

Fill in the template when you [open an issue](../../issues/new/choose). Most useful:

- Device model and Android version, Windows version, EkranLink version (bottom left of the window)
- Connection (USB / Wi-Fi) and display mode (Extend / Only this / Mirror)
- Log: *General > Open log* in the window. Look through it before sharing; it may contain device names and
  local IP addresses.

It is also valuable to hear that EkranLink worked, or did not, on a device we have not tested: use the
*Device report* template; results go into [docs/DEVICES.md](docs/DEVICES.md).

## Contributing code

- Building: see [README](README.md#building-from-source). `.\tools\paketle.ps1` must finish without errors
  before you send a change (it also runs the tests).
- Small changes with one topic are preferred. Before a large change, open an issue so we can talk it over.
- Priorities: user experience (changing a setting must not make the screen flicker; names consistent with
  Windows) and low system load (low-power devices and headless mini PCs are targets too).
- Match the surrounding code: comments and log lines are in Turkish, user-facing text in both Turkish and
  English (`host-windows/src/i18n.h`, `android/app/src/main/res/values*/strings.xml`). Documents come in pairs:
  `X.md` (English) and `X.tr.md` (Turkish); update both.
- The Android side must keep working on Android 4.1 (API 16): use newer APIs behind a version check.
- If you use an AI coding assistant, point it to [AGENTS.md](AGENTS.md).

## License

Contributions are published under the license of the directory they are added to: GPL-3.0 for the repository
in general, MS-PL / MIT in `driver/` ([driver/LICENSE](driver/LICENSE)). If you bring code from another
project, state its source and license.

---

## Türkçe

Issue ve pull request'leri Türkçe ya da İngilizce yazabilirsiniz.

### Hata bildirmek

[Yeni issue](../../issues/new/choose) açarken şablonu doldurun. En çok işe yarayanlar:

- Cihaz modeli ve Android sürümü, Windows sürümü, EkranLink sürümü (pencerenin sol altında)
- Bağlantı türü (USB / Wi-Fi) ve görüntü modu (İkinci ekran / Tek ekran / Yansıtma)
- Günlük: pencerede *Genel > Günlüğü aç*. Paylaşmadan önce göz atın; cihaz adları ve yerel IP adresleri
  içerebilir.

EkranLink'in denemediğimiz bir cihazda çalıştığını ya da çalışmadığını bilmek de değerli: *Cihaz test sonucu*
şablonunu kullanın; sonuçlar [docs/DEVICES.md](docs/DEVICES.md) listesine eklenir.

### Kod katkısı

- Derleme: [README](README.tr.md#kaynaktan-derlemek). Göndermeden önce `.\tools\paketle.ps1` hatasız bitmeli
  (testleri de çalıştırır).
- Küçük, tek konulu değişiklikler tercih edilir. Büyük bir değişiklikten önce issue açıp konuşalım.
- Öncelikler: kullanıcı deneyimi (ayar değiştirmek ekranı kırpıştırmamalı, adlar Windows'la tutarlı) ve
  düşük sistem yükü (düşük güçlü cihazlar ve ekransız mini PC'ler de hedefte).
- Kod çevresindeki koda benzesin: yorumlar ve günlük satırları Türkçe, kullanıcıya görünen metinler hem
  Türkçe hem İngilizce (`host-windows/src/i18n.h`, `android/app/src/main/res/values*/strings.xml`). Belgeler
  çift: `X.md` (İngilizce) ve `X.tr.md` (Türkçe); ikisini birlikte güncelleyin.
- Android tarafı Android 4.1'de (API 16) de çalışmalı; yeni API'leri sürüm denetimiyle kullanın.
- Yapay zekâ kodlama asistanı kullanıyorsanız ona [AGENTS.md](AGENTS.md)'yi gösterin.

### Lisans

Gönderdiğiniz katkılar, eklendikleri klasörün lisansıyla yayımlanır: depo genelinde GPL-3.0, `driver/`
klasöründe MS-PL / MIT ([driver/LICENSE](driver/LICENSE)). Başka bir projeden kod alıyorsanız kaynağını ve
lisansını belirtin.
