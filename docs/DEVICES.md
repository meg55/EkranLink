# Cihaz uyumluluk listesi

Test ettiğiniz cihazı ekleyin (pull request veya issue). Değerler için: `tools/faz0.ps1` çalışırken
tabletteki ölçüm katmanını okuyun.

| Cihaz | Android | SoC / çözücü | Ekran (kullanılabilir) | USB yolları | Sonuç |
|---|---|---|---|---|---|
| Lenovo IdeaTab S6000-H (Vodafone Smart Tab III 10) | 4.2.2 (API 17) | MediaTek MT8389, `OMX.MTK.VIDEO.DECODER.AVC` | 1280×800 (1280×752) | ADB ✅, AOA çekirdek desteği var, RNDIS çekirdek desteği var | 1280×752 @ 120 fps kare düşmeden; çözücü ~3 kare tutuyor (60 fps'te ~58 ms, 120 fps'te ~33 ms). Wi-Fi yalnızca 2.4 GHz |
