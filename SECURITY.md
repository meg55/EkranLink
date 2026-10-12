# Security / Güvenlik

**English** · [Türkçe](#türkçe)

EkranLink sends the screen picture over the network, passes touch to Windows as mouse / touch input, can run
as a Windows service (SYSTEM) and installs a display driver. We take security issues seriously.

## Reporting

Please report vulnerabilities **privately, not as public issues**, using GitHub's private reporting:
*Security > Report a vulnerability* on the repository page. English or Turkish is fine.

We aim to reply within a week. Please keep details private until a fix is released; reporters are credited in
the release notes if they wish.

## Scope

We are especially interested in:

- Wi-Fi pairing and encryption (ECDH P-256 + ChaCha20-Poly1305, [docs/PROTOCOL.md](docs/PROTOCOL.md)): an
  unpaired device receiving the picture or sending input
- Privilege escalation through the communication between the service and the user session (registry)
- Commands sent to the virtual display driver (`driver/`)
- Parsing of data from the network (discovery, handshake, video and touch messages)

What someone with physical access to a device with USB debugging enabled can do is part of Android's own
security model and out of scope.

## Known limitations

- **Over USB the picture sent to the device is not encrypted** (to spare old tablets' CPUs). It only travels
  over the cable and the local loopback; reading it needs administrator rights on the PC or root on the
  device. Everything the device sends to the PC, including touch, is always encrypted and authenticated.
- **PCs shared by several Windows users, in service mode:** pairings are machine-wide, so a device paired by
  one user reconnects for whoever is at the console, and any local user can clear the pairings.
- **adb:** in service mode the adb server runs as SYSTEM with the user's adb key while EkranLink uses it;
  like any adb server, it accepts connections from local programs.

## Supported versions

Only the latest release receives security fixes.

---

## Türkçe

EkranLink ekran görüntüsünü ağ üzerinden gönderir, dokunmayı Windows'a fare / dokunmatik girdisi olarak
iletir, isteğe bağlı olarak Windows hizmeti (SYSTEM) olarak çalışır ve bir ekran sürücüsü kurar. Bu yüzden
güvenlik açıklarını ciddiye alıyoruz.

### Bildirme

Güvenlik açıklarını **herkese açık issue olarak değil**, GitHub'ın gizli bildirimiyle gönderin: depo
sayfasında *Security > Report a vulnerability*. Türkçe ya da İngilizce yazabilirsiniz.

Yanıt hedefi: bir hafta içinde ilk dönüş. Düzeltme yayımlanana kadar ayrıntıları paylaşmamanızı rica ederiz;
bildirene, isterse sürüm notlarında teşekkür edilir.

### Kapsam

Özellikle ilgilendiklerimiz:

- Wi-Fi eşleştirme ve şifreleme (ECDH P-256 + ChaCha20-Poly1305, [docs/PROTOCOL.tr.md](docs/PROTOCOL.tr.md)):
  eşleşmemiş bir cihazın görüntü alması ya da girdi göndermesi
- Hizmet ile kullanıcı oturumu arasındaki iletişim (kayıt defteri) üzerinden yetki yükseltme
- Sanal ekran sürücüsüne gönderilen komutlar (`driver/`)
- Ağdan gelen verinin ayrıştırılması (keşif, el sıkışma, video ve dokunma iletileri)

USB hata ayıklama açık bir cihaza fiziksel erişimi olan birinin yapabildikleri, Android'in kendi güvenlik
modelinin parçasıdır ve kapsam dışıdır.

### Bilinen sınırlar

- **USB'de cihaza giden görüntü şifrelenmez** (eski tabletlerin işlemcisini yormamak için). Yalnızca kablo ve
  yerel geri döngü üzerinden gider; okumak için PC'de yönetici yetkisi ya da cihazda root gerekir. Cihazın
  PC'ye gönderdiği her şey, dokunma dahil, her zaman şifreli ve doğrulanmıştır.
- **Birden çok Windows kullanıcısının paylaştığı PC'lerde, hizmet modunda:** eşleştirmeler makine geneldir;
  bir kullanıcının eşleştirdiği cihaz o an ekranda kim varsa ona bağlanır ve her yerel kullanıcı
  eşleştirmeleri silebilir.
- **adb:** hizmet modunda EkranLink kullanırken adb sunucusu kullanıcının adb anahtarıyla SYSTEM olarak çalışır;
  her adb sunucusu gibi yerel programların bağlantısını kabul eder.

### Desteklenen sürümler

Yalnızca en son sürüm güvenlik düzeltmesi alır.
