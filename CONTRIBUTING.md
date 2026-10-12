# Katkı / Contributing

Türkçe ya da İngilizce yazabilirsiniz. *Issues and pull requests are welcome in Turkish or English.*

## Hata bildirmek

[Yeni issue](../../issues/new/choose) açarken şablonu doldurun. En çok işe yarayanlar:

- Cihaz modeli ve Android sürümü, Windows sürümü, EkranLink sürümü (pencerenin sol altında)
- Bağlantı türü (USB / Wi-Fi) ve görüntü modu (ikinci ekran / tek ekran / yansıtma)
- Günlük: pencerede *Genel > Günlüğü aç*. Paylaşmadan önce göz atın; cihaz adları ve yerel IP adresleri
  içerebilir.

Yeni bir cihazda çalıştıysa ya da çalışmadıysa bu da değerli: [docs/DEVICES.md](docs/DEVICES.md).

## Kod katkısı

- Derleme: [README](README.tr.md#kaynaktan-derlemek). Göndermeden önce `.\tools\paketle.ps1` hatasız bitmeli (testleri de
  çalıştırır).
- Küçük, tek konulu değişiklikler tercih edilir. Büyük bir değişiklikten önce issue açıp konuşalım.
- Öncelikler: kullanıcı deneyimi (ayar değiştirmek ekranı kırpıştırmamalı, adlar Windows'la tutarlı) ve
  düşük sistem yükü (düşük güçlü cihazlar ve ekransız mini PC'ler de hedefte).
- Kod çevresindeki koda benzesin: yorumlar ve günlük satırları Türkçe, kullanıcıya görünen metinler hem
  Türkçe hem İngilizce (`host-windows/src/i18n.h`, `android/app/src/main/res/values*/strings.xml`).
- Android tarafı Android 4.1'de (API 16) de çalışmalı; yeni API'leri sürüm denetimiyle kullanın.

## Lisans

Gönderdiğiniz katkılar, eklendikleri klasörün lisansıyla yayımlanır: depo genelinde GPL-3.0,
`driver/` klasöründe MS-PL / MIT ([driver/LICENSE](driver/LICENSE)). Başka bir projeden kod alıyorsanız
kaynağını ve lisansını belirtin.
