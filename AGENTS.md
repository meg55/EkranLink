# AGENTS.md

> **Türkçe:** Bu dosya yapay zekâ kodlama asistanları içindir (Codex, Copilot, Cursor, Claude vb. depoda bu adı
> arar), bu yüzden İngilizcedir. Proje belgelerinin Türkçeleri: [README.md](README.md) (Türkçe bölümü),
> [docs/ARCHITECTURE.tr.md](docs/ARCHITECTURE.tr.md), [docs/PROTOCOL.tr.md](docs/PROTOCOL.tr.md),
> [docs/ROADMAP.tr.md](docs/ROADMAP.tr.md), [driver/README.tr.md](driver/README.tr.md).

Guidance for AI coding agents, and a quick orientation for human contributors. Read this before changing
code. For what the project is, see [README.md](README.md); for how it works, [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## The project in one paragraph

EkranLink makes an Android tablet or phone a monitor for a Windows PC, over USB (adb) or Wi-Fi. The Windows
side (C++20, MinGW, Win32 / D3D11 / Media Foundation, no third-party libraries) captures the desktop on the
GPU, encodes H.264 in hardware and streams it; the Android app (Java, runs on API 16 through 36) decodes it
with MediaCodec and sends touch back. A user-mode IddCx driver (`driver/`) adds a real virtual monitor per
device. It can run as a Windows service so the sign-in screen appears on the device too.

## Where to look

| Task | Files |
|---|---|
| Settings window (pages, controls) | `host-windows/src/ui.cpp`, `ui.h` (Direct2D, drawn by hand) |
| Tray icon, menu, glue between UI and streaming | `host-windows/src/tray.cpp` |
| User-visible PC strings (Turkish + English) | `host-windows/src/i18n.h` (`X(name, tr, en)`) |
| Device discovery, connect / disconnect, sessions | `host-windows/src/streamer.cpp`, `adb.cpp`, `discovery.cpp`, `usb_watch.cpp` |
| Capture, scaling, encoding | `capture.cpp`, `scaler.cpp`, `encoder.cpp`, `orientation.h` |
| Touch / mouse injection | `input.cpp` |
| Virtual monitor, Only-this-screen layout | `virtual_display.cpp`; driver side `driver/src/Driver.cpp` |
| Wire protocol | `protocol.h`, `net.cpp`; Android `Session.java`; spec `docs/PROTOCOL.md` |
| Pairing and encryption | `pairing.cpp`, `crypto.cpp`, `chacha.cpp`; Android `Crypto.java`, `ChaCha20.java`, `Poly1305.java`, `PairStore.java` |
| Settings storage (registry), service IPC | `config.cpp`, `config.h`, `service.cpp` |
| Android screen, touch, orientation | `MainActivity.java`, `CursorView.java`, `OrientationWatcher.java`, `InfoLayer.java` |
| Android user-visible strings | `android/app/src/main/res/values/strings.xml` (English) and `values-tr/` (Turkish) |
| Installer | `host-windows/installer/EkranLink.iss` (Inno Setup) |
| Packaging, CI | `tools/paketle.ps1`, `.github/workflows/derle.yml` |
| Icons and store graphics | `tools/simge.py` (generates everything in `art/` and the app icons) |
| Version | `host-windows/src/version.h` and `android/app/build.gradle` (must match) |

## Build and test

Windows only. PowerShell commands from the repository root:

```powershell
.\tools\paketle.ps1          # full build: Android (Gradle) + Windows (CMake/Ninja) + crypto tests + installer, zip, APK in dist\
.\host-windows\build.ps1     # Windows programs only -> host-windows\build\
.\host-windows\build\ekranlink-test.exe    # crypto test vectors; must print no failures
cd android; .\gradlew.bat assembleDebug    # Android only (needs JDK 17+; Android Studio's jbr works)
.\driver\build.ps1           # driver (needs MSVC + WDK 10.0.26100); installing it needs administrator
```

There are no other automated tests. Behaviour is verified on real hardware: run
`host-windows\build\ekranlink-host.exe` (console, per-second latency / bitrate / fps), use `adb shell input
tap x y` to check touch mapping, `tools/sayac.html` for latency, and `tools/faz0.ps1` for a raw-stream test.
Test devices so far: Lenovo S6000 (Android 4.2.2, MediaTek decoder) and the Android 16 emulator. A change that
compiles is not a change that works: say what you verified and what you could not.

## Hard rules

- **Never commit secrets.** The APK release key and its passwords live outside the repository (path in
  `~/.gradle/gradle.properties` as `releaseKeystoreProperties`, or `RELEASE_KEYSTORE_PROPERTIES`). Do not add
  keys, passwords, certificates or tokens to code, docs, CI files or logs.
- **Android API 16 must keep working.** No AndroidX, no Java language level above 8, no API newer than 16
  without a `Build.VERSION.SDK_INT` check. Check every new Android call against its API level.
- **Licenses.** Everything is GPL-3.0 except `driver/`, which is MS-PL / MIT and a separate program. Do not
  copy code between `driver/` and the rest. Third-party code needs a compatible license and attribution.
- **Both languages.** Every user-visible string exists in Turkish and English (`i18n.h`; Android `values` and
  `values-tr`). Use the terms Windows itself uses (e.g. *Extend*, *Display resolution*).
- **Protocol changes** go to both sides and to `docs/PROTOCOL.md`. Older apps on the other side must get the
  `Incompatible` message, never a silent failure.
- **Version** is bumped in `version.h` and `build.gradle` together (`versionCode` too).
- **The PC never installs the APK on the device.** The two apps are separate downloads.

## Design principles

- **Low system load is a feature.** Keep the picture on the GPU; no CPU copies of frames; nothing sent while
  the screen is still; no polling while idle (use Windows notifications). Measure CPU before and after
  changes in the streaming path.
- **Settings apply without dropping the connection or flickering the screen.** Rebuild only the part that
  changed.
- **Fail safe.** If the app dies, the driver removes its monitors after 3 seconds; restore the monitor layout
  after *Only this*; never leave the user without a visible screen.

## Conventions

- C++20 with MinGW GCC; Win32 APIs directly (CNG for crypto, Media Foundation, D3D11, Direct2D).
- Source comments, log lines and commit messages are in Turkish; identifiers are mostly English, some
  Turkish (see glossary). Match the style of the surrounding code.
- Text files are UTF-8. `.ps1`, `.bat` and `.iss` use CRLF (`.gitattributes`); PowerShell scripts and the
  `.iss` file start with a BOM so Windows PowerShell 5 and Inno Setup read Turkish characters correctly.
- **Every document exists in English and Turkish.** `README.md` holds both: an English section (open) and a
  Turkish section (collapsed `<details>`); keep them in sync. Other long documents come in pairs, `X.md`
  (English) and `X.tr.md` (Turkish): `docs/ARCHITECTURE`, `docs/PROTOCOL`, `docs/ROADMAP`, `driver/README`. Files
  GitHub looks up by name (`CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`) and `docs/DEVICES.md` hold
  both languages in one file, English first. When you change a document, change both languages in the same
  commit; links in Turkish documents point to `.tr.md` versions. This file is the exception (English only).

## Glossary (Turkish names in code, files and docs)

| Turkish | English |
|---|---|
| ajan | agent (service-mode streaming process, `--ajan`) |
| anahtar kare | keyframe |
| arka plan (`--arka-plan`) | background (start without opening the window) |
| ayarlar, `Ayarlar` | settings (registry key in service mode) |
| bağlan / bağlantıyı kes | connect / disconnect |
| BENIOKU | README (text file in the zip) |
| cihaz, `Cihazlar` | device, device list (registry value) |
| çözücü / kodlayıcı | decoder / encoder |
| çözünürlük | resolution |
| derle, derleme | build |
| durum, `Durum` | status (registry key written by the agent) |
| eşleşme, eşleştirme | pairing |
| faz0 | phase 0 (raw H.264 stream test) |
| `Gizli` | secret (registry key, SYSTEM only) |
| günlük | log |
| hizmet (`--hizmet`) | Windows service |
| ikinci ekran / tek ekran / yansıtma | Extend / Only this / Mirror |
| itici kare | pusher frame (extra frames after a change so decoders release the last frame) |
| `Izle` | "watching" flag: the settings window is open, the agent publishes stats |
| kimlik | ID (device or PC identifier) |
| `Komut`, `KomutNo` | command and command counter (tray → agent) |
| kur / kaldır (`--hizmet-kur`, `kur.ps1`) | install / uninstall |
| kurulum | installer, setup |
| paketle | package (build release files) |
| sayaç | counter (latency test page) |
| seri | (adb) serial number |
| simge | icon |
| `SonYon` | last orientation (per device) |
| sürücü | driver |
| sürüm | version, release |
| taslak | draft (GitHub release) |
| tepsi | tray (notification area icon) |
| windows ile başlat | start with Windows |
| yön | orientation |

## Documents

Each has a Turkish twin (`.tr.md`) except where noted.

- [README.md](README.md): what it is, how to use it
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): programs, frame path, connections, service mode
- [docs/PROTOCOL.md](docs/PROTOCOL.md): wire protocol, handshake, encryption
- [docs/ROADMAP.md](docs/ROADMAP.md): plan, decisions and measurements
- [docs/DEVICES.md](docs/DEVICES.md): tested devices (both languages in one file)
- [driver/README.md](driver/README.md): virtual display driver, signing, install
- [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md), [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md),
  [PRIVACY.md](PRIVACY.md) (both languages in one file)
