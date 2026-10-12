# Tested devices / Test edilen cihazlar

Add the device you tested with a pull request, or open an issue with the *Device report* template. For the
numbers, read the performance overlay on the device (three-finger tap).

Test ettiğiniz cihazı pull request ile ekleyin ya da *Cihaz test sonucu* şablonuyla issue açın. Değerler için
cihazdaki ölçüm kartını okuyun (üç parmakla dokunun).

| Device / Cihaz | Android | SoC / decoder (çözücü) | Screen (usable) / Ekran (kullanılabilir) | USB | Result / Sonuç |
|---|---|---|---|---|---|
| Lenovo IdeaTab S6000-H (Vodafone Smart Tab III 10) | 4.2.2 (API 17) | MediaTek MT8389, `OMX.MTK.VIDEO.DECODER.AVC` | 1280×800 (1280×752) | ADB ✅; kernel supports AOA and RNDIS / çekirdekte AOA ve RNDIS desteği var | 1280×752 @ 120 fps with no dropped frames; the decoder holds ~3 frames (~58 ms at 60 fps, ~33 ms at 120 fps). Wi-Fi 2.4 GHz only. 0.5.0: USB and encrypted Wi-Fi, touch exact (0 px), Wi-Fi PC→draw ~78–94 ms, lock screen shown in *Only this*. / 1280×752 @ 120 fps kare düşmeden; çözücü ~3 kare tutuyor (60 fps'te ~58 ms, 120 fps'te ~33 ms). Wi-Fi yalnızca 2.4 GHz. 0.5.0: USB ve şifreli Wi-Fi, dokunma tam (0 px), Wi-Fi'de PC→çizim ~78–94 ms, *Tek ekran*da kilit ekranı görünüyor. |
| Android 16 emulator / öykünücü (sw720dp tablet, phone) | 16 (API 36) | x86_64, software / yazılım | various / çeşitli | ADB ✅ | Portrait windows on large screens, touch ±1 px in all four orientations, two devices at once. / Büyük ekranda dikey pencere, dört yönde dokunma ±1 px, iki cihaz aynı anda. |
