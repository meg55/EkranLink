# Güvenlik / Security

EkranLink ekran görüntüsünü ağ üzerinden gönderir, dokunmayı Windows'a fare / dokunmatik girdisi olarak
iletir, isteğe bağlı olarak Windows hizmeti (SYSTEM) olarak çalışır ve bir ekran sürücüsü kurar. Bu yüzden
güvenlik açıklarını ciddiye alıyoruz.

## Bildirme

Güvenlik açıklarını **herkese açık issue olarak değil**, GitHub'ın gizli bildirimiyle gönderin:
depo sayfasında *Security > Report a vulnerability*. Türkçe ya da İngilizce yazabilirsiniz.

*Please do not report security vulnerabilities through public issues. Use GitHub's private reporting:
Security > Report a vulnerability.*

Yanıt hedefi: bir hafta içinde ilk dönüş. Düzeltme yayımlanana kadar ayrıntıları paylaşmamanızı rica ederiz;
bildirene, isterse sürüm notlarında teşekkür edilir.

## Kapsam

Özellikle ilgilendiklerimiz:

- Wi-Fi eşleştirme ve şifreleme (ECDH P-256 + ChaCha20-Poly1305, [docs/PROTOCOL.md](docs/PROTOCOL.md)):
  eşleşmemiş bir cihazın görüntü alması ya da girdi göndermesi
- Hizmet ile kullanıcı oturumu arasındaki iletişim (kayıt defteri) üzerinden yetki yükseltme
- Sanal ekran sürücüsüne gönderilen komutlar (`driver/`)
- Ağdan gelen verinin ayrıştırılması (keşif, el sıkışma, video ve dokunma iletileri)

USB hata ayıklama açık bir cihaza fiziksel erişimi olan birinin yapabildikleri, Android'in kendi güvenlik
modelinin parçasıdır ve kapsam dışıdır.

## Desteklenen sürümler

Yalnızca en son sürüm güvenlik düzeltmesi alır.
