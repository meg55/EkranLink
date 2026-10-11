"""EkranLink simgesini tek tanımdan üretir (Pillow gerekir: pip install pillow).

    python tools/simge.py

Çıktılar (depoya eklenir, her değişiklikte yeniden üretilir):
  art/ekranlink.svg                         kaynak çizim (1024 birim)
  art/play-icon-512.png                     Google Play simgesi (köşeleri Play yuvarlar)
  art/play-feature-tr.png, -en.png          Google Play tanıtım görseli (1024×500)
  host-windows/res/ekranlink.ico            Windows: exe, pencere ve bağlıyken tepsi
  host-windows/res/ekranlink-bosta.ico      Windows: bağlı değilken tepsi (gri)
  android/.../mipmap-*/ic_launcher.png      Android 8 öncesi başlatıcı
  android/.../drawable/ic_launcher_*.xml    Android 8+ uyarlanabilir simge (vektör)
  android/.../drawable-*/ic_stat_ekranlink.png  bildirim çubuğu (beyaz siluet)

Tasarım: büyük ekran (PC monitörü) ve yanında tablet; ikisinin ekranı aynı renkte, yani aynı görüntü.
Küçük boyutlarda (≤32 px) çerçeveler kalınlaşır ki tepside de okunsun.
"""
import os
from PIL import Image, ImageDraw, ImageFont

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(KOK, 'android', 'app', 'src', 'main', 'res')

ARKA_UST, ARKA_ALT = (59, 108, 246), (30, 63, 174)      # arka plan: sol üstten sağ alta mavi
EKRAN_UST, EKRAN_ALT = (186, 242, 255), (95, 212, 245)   # ekranlar: açık camgöbeği
BEYAZ = (255, 255, 255)


def sekil(kucuk=False):
    """1024 birimlik tuvalde parçalar: (ad, (x0, y0, x1, y1), köşe yarıçapı)."""
    if not kucuk:
        f_m, f_t = 44, 28
        return [
            ('cerceve', (145, 285, 675, 635), 48), ('ekran', (145 + f_m, 285 + f_m, 675 - f_m, 635 - f_m), 12),
            ('cerceve', (382, 635, 438, 692), 0), ('cerceve', (300, 690, 520, 738), 22),
            ('cerceve', (710, 415, 880, 740), 34), ('ekran', (710 + f_t, 415 + f_t, 880 - f_t, 740 - f_t), 8),
        ]
    f_m, f_t = 84, 64
    return [
        ('cerceve', (90, 240, 690, 640), 70), ('ekran', (90 + f_m, 240 + f_m, 690 - f_m, 640 - f_m), 16),
        ('cerceve', (340, 640, 440, 720), 0), ('cerceve', (220, 700, 560, 800), 40),
        ('cerceve', (720, 380, 950, 800), 56), ('ekran', (720 + f_t, 380 + f_t, 950 - f_t, 800 - f_t), 10),
    ]


def egim(w, h, ust, alt, capraz=True):
    """Doğrusal renk geçişi (çapraz: sol üst → sağ alt; değilse yukarıdan aşağı)."""
    im = Image.new('RGB', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            t = ((x / max(1, w - 1)) + (y / max(1, h - 1))) / 2 if capraz else y / max(1, h - 1)
            px[x, y] = tuple(round(a + (b - a) * t) for a, b in zip(ust, alt))
    return im


def ciz(boyut, arka='yuvarlak', kenar=0.0, kucuk=None, siluet=False, olcek=1.0):
    """Tek boyutta simge. arka: 'yuvarlak' (köşeli kare), 'kare' (tam), None (saydam).
    kenar: tuvalin kenar boşluğu oranı. siluet: yalnızca beyaz çerçeveler (bildirim simgesi)."""
    if kucuk is None:
        kucuk = boyut <= 32
    S = 8                                            # süper örnekleme
    N = boyut * S
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    ic = N * (1 - 2 * kenar)                          # içerik kutusu
    o = N * kenar

    def k(v):                                         # 1024 birim → piksel
        return o + v / 1024 * ic

    if arka:
        maske = Image.new('L', (N, N), 0)
        r = 0 if arka == 'kare' else ic * 0.22
        ImageDraw.Draw(maske).rounded_rectangle((o, o, o + ic - 1, o + ic - 1), radius=r, fill=255)
        im.paste(egim(N // 4, N // 4, ARKA_UST, ARKA_ALT).resize((N, N), Image.BILINEAR), (0, 0), maske)

    def kutu(b):                                      # içeriği ortadan ölçekle (olcek < 1: küçült)
        c = 512
        return [k(c + (v - c) * olcek) for v in b]

    for ad, b, r in sekil(kucuk):
        x0, y0, x1, y1 = kutu(b)
        rr = r / 1024 * ic * olcek
        if ad == 'cerceve':
            ImageDraw.Draw(im).rounded_rectangle((x0, y0, x1, y1), radius=rr, fill=BEYAZ + (255,))
        else:
            maske = Image.new('L', (N, N), 0)
            ImageDraw.Draw(maske).rounded_rectangle((x0, y0, x1, y1), radius=rr, fill=255)
            if siluet:
                im.paste((0, 0, 0, 0), (0, 0), maske)  # ekran saydam: yalnızca çerçeveler
            else:
                w, h = max(1, int(x1 - x0)), max(1, int(y1 - y0))
                dolgu = egim(max(2, w // 8), max(2, h // 8), EKRAN_UST, EKRAN_ALT, False).resize((w, h), Image.BILINEAR)
                katman = Image.new('RGBA', (N, N), (0, 0, 0, 0))
                katman.paste(dolgu, (int(x0), int(y0)))
                im.paste(katman, (0, 0), maske)
    return im.resize((boyut, boyut), Image.LANCZOS)


def gri(im):
    a = im.getchannel('A')
    g = im.convert('L').point(lambda v: int(v * 0.85)).convert('RGBA')
    g.putalpha(a)
    return g


def yol(*p):
    d = os.path.join(KOK, *p)
    os.makedirs(os.path.dirname(d), exist_ok=True)
    return d


def ico(ad, donustur=lambda i: i):
    boyutlar = [16, 20, 24, 32, 40, 48, 64, 256]
    resimler = [donustur(ciz(b, kenar=0.03 if b <= 32 else 0.06)) for b in boyutlar]
    resimler[-1].save(yol('host-windows', 'res', ad), format='ICO', sizes=[(b, b) for b in boyutlar],
                      append_images=resimler[:-1])


def rr_yol(x0, y0, x1, y1, r):
    """Android vektörü için yuvarlak köşeli dikdörtgen yolu."""
    f = lambda v: ('%.2f' % v).rstrip('0').rstrip('.')
    if r <= 0:
        return 'M%s,%sH%sV%sH%sZ' % (f(x0), f(y0), f(x1), f(y1), f(x0))
    return ('M%s,%sH%sA%s,%s 0 0 1 %s,%sV%sA%s,%s 0 0 1 %s,%sH%sA%s,%s 0 0 1 %s,%sV%sA%s,%s 0 0 1 %s,%sZ' % (
        f(x0 + r), f(y0), f(x1 - r), f(r), f(r), f(x1), f(y0 + r), f(y1 - r), f(r), f(r), f(x1 - r), f(y1),
        f(x0 + r), f(r), f(r), f(x0), f(y1 - r), f(y0 + r), f(r), f(r), f(x0 + r), f(y0)))


def uyarlanabilir():
    # 108×108 tuval; görünür alan ortadaki 66 çaplı daire: içerik ~56 birim genişliğe sığdırılır
    s = 56 / 735
    d = lambda v: 54 + (v - 512) * s
    cerceveler, ekranlar = [], []
    for ad, (x0, y0, x1, y1), r in sekil():
        p = rr_yol(d(x0), d(y0), d(x1), d(y1), r * s)
        (cerceveler if ad == 'cerceve' else ekranlar).append(p)
    on = ('<?xml version="1.0" encoding="utf-8"?>\n'
          '<!-- tools/simge.py üretir; elle değiştirmeyin -->\n'
          '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
          '    android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">\n'
          '    <path android:fillColor="#FFFFFF" android:pathData="%s" />\n'
          '    <path android:fillColor="#8BE3FA" android:pathData="%s" />\n'
          '</vector>\n') % (''.join(cerceveler), ''.join(ekranlar))
    arka = ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<!-- tools/simge.py üretir; elle değiştirmeyin -->\n'
            '<shape xmlns:android="http://schemas.android.com/apk/res/android">\n'
            '    <gradient android:angle="315" android:startColor="#%02X%02X%02X" android:endColor="#%02X%02X%02X" />\n'
            '</shape>\n') % (ARKA_UST + ARKA_ALT)
    uyarla = ('<?xml version="1.0" encoding="utf-8"?>\n'
              '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
              '    <background android:drawable="@drawable/ic_launcher_background" />\n'
              '    <foreground android:drawable="@drawable/ic_launcher_foreground" />\n'
              '</adaptive-icon>\n')
    for ad, metin in (('drawable/ic_launcher_foreground.xml', on), ('drawable/ic_launcher_background.xml', arka),
                      ('mipmap-anydpi-v26/ic_launcher.xml', uyarla)):
        with open(yol('android', 'app', 'src', 'main', 'res', *ad.split('/')), 'w', encoding='utf-8', newline='\n') as f:
            f.write(metin)


def svg():
    parca = []
    for ad, (x0, y0, x1, y1), r in sekil():
        dolgu = '#FFFFFF' if ad == 'cerceve' else 'url(#ekran)'
        parca.append('  <rect x="%d" y="%d" width="%d" height="%d" rx="%d" fill="%s"/>' % (x0, y0, x1 - x0, y1 - y0, r, dolgu))
    h = lambda c: '#%02X%02X%02X' % c
    metin = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">\n'
             '  <!-- EkranLink simgesi. Kaynak: tools/simge.py -->\n'
             '  <defs>\n'
             '    <linearGradient id="arka" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>\n'
             '    <linearGradient id="ekran" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>\n'
             '  </defs>\n'
             '  <rect width="1024" height="1024" rx="225" fill="url(#arka)"/>\n%s\n</svg>\n') % (
        h(ARKA_UST), h(ARKA_ALT), h(EKRAN_UST), h(EKRAN_ALT), '\n'.join(parca))
    with open(yol('art', 'ekranlink.svg'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(metin)


def tanitim(dil, alt_yazi):
    W, H = 1024, 500
    im = egim(W // 4, H // 4, ARKA_UST, ARKA_ALT).resize((W, H), Image.BILINEAR).convert('RGBA')
    logo = ciz(300, arka=None)
    im.alpha_composite(logo, (70, 100))
    yazi_yolu = r'C:\Program Files\Android\Android Studio\jbr\lib\fonts'
    kalin = ImageFont.truetype(os.path.join(yazi_yolu, 'DroidSans-Bold.ttf'), 104)
    normal = ImageFont.truetype(os.path.join(yazi_yolu, 'DroidSans.ttf'), 38)
    d = ImageDraw.Draw(im)
    d.text((410, 150), 'EkranLink', font=kalin, fill=BEYAZ)
    y = 290
    for satir in alt_yazi:
        d.text((414, y), satir, font=normal, fill=(225, 236, 255))
        y += 50
    im.convert('RGB').save(yol('art', 'play-feature-%s.png' % dil))


def main():
    svg()
    ciz(512, arka='kare').convert('RGB').save(yol('art', 'play-icon-512.png'))
    tanitim('tr', ['Eski tablet ve telefonları', 'bilgisayarınıza ekran yapın'])
    tanitim('en', ['Turn old tablets and phones', 'into displays for your PC'])
    ico('ekranlink.ico')
    ico('ekranlink-bosta.ico', gri)
    for yogunluk, olcu in (('mdpi', 1), ('hdpi', 1.5), ('xhdpi', 2), ('xxhdpi', 3), ('xxxhdpi', 4)):
        b = round(48 * olcu)
        ciz(b, kenar=2 / 48, kucuk=False).save(yol('android', 'app', 'src', 'main', 'res', 'mipmap-' + yogunluk, 'ic_launcher.png'))
        s = round(24 * olcu)
        ciz(s, arka=None, kucuk=True, siluet=True, kenar=1 / 24).save(
            yol('android', 'app', 'src', 'main', 'res', 'drawable-' + yogunluk, 'ic_stat_ekranlink.png'))
    uyarlanabilir()
    print('tamam')


if __name__ == '__main__':
    main()
