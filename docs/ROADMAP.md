**English** · [Türkçe](ROADMAP.tr.md)

# EkranLink roadmap

Open-source app that turns Android tablets and phones into displays for a Windows PC: Mirror, Extend
or Windows' only screen (Only this); over USB or Wi-Fi.

## Decisions

| Topic | Decision | Why |
|---|---|---|
| Name | EkranLink (package `org.ekranlink.android`, Windows service `EkranLink`); EKRAN up to 0.4.x | A more general, international name; no conflicting app or trademark found (2026-10) |
| License | GPL-3.0 | To reuse code and old-device fixes from GPL projects like Moonlight/Sunshine; derivatives stay open |
| Windows side | C++ (service, capture, encoding, driver) | The IddCx driver is C++ anyway; all DXGI/NVENC/AMF samples are C/C++ |
| Android client | Java, minSdk 16 (4.1), no AndroidX | MediaCodec arrived in 4.1; current AndroidX/NDK don't support 4.x |
| iOS client | Swift, VideoToolbox | A native app is wanted; very old iOS versions via the web client |
| Web client | Framework-free JavaScript | Old browsers |
| Target PCs | Windows 10 1809+ / 11; headless mini PCs and desktops | The "Only this screen" scenario is a first-class citizen |
| Virtual display | [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver) (MIT, signed) at first, then our own IddCx driver | Make progress early without driver-signing headaches |
| Encryption | Ephemeral ECDH P-256 + ChaCha20-Poly1305, 6-digit pairing code on the first Wi-Fi connection ([PROTOCOL.md](PROTOCOL.md)) | Android 4.x's TLS stack is outdated; ChaCha20 is fast on CPUs without hardware AES |

## Known limitations

- BIOS, boot, Safe Mode and the BitLocker PIN screen cannot be shown by software (see experimental HDMI mode).
- Android 4.x has no `adb reverse`: the PC always initiates the connection (`adb forward`).
- Before Android 4.4 the navigation bar can't be hidden permanently: the virtual monitor is created at the
  tablet's usable area (1280×752 on the S6000).
- DRM-protected content may appear black on the virtual display.

## Phase 0 – Smoke test ✅

Goal: measure the old tablet's hardware decoder and base the design on real data.

Test device: Lenovo IdeaTab S6000, Android 4.2.2, MediaTek MT8389, `OMX.MTK.VIDEO.DECODER.AVC`.
PC: RTX 2060 (NVENC). Stream: 1280×752 H.264 Constrained Baseline, no B-frames.

| Connection | fps | Decoder latency (avg) | Dropped frames |
|---|---|---|---|
| USB (adb forward) | 30 | ~107 ms | 0 |
| USB (adb forward) | 60 | ~58 ms | 0 |
| USB (adb forward) | 90 | ~41 ms | 0 |
| USB (adb forward) | 120 | ~33 ms | 0 |
| Wi-Fi 2.4 GHz | 60 | ~58 ms | 0 |
| Wi-Fi 2.4 GHz | 120 | ~35 ms | 0 |

Findings:

1. The MTK decoder **holds a constant ~3 frames internally** regardless of fps. Latency scales with the
   number of frames, not with decode time.
2. What didn't work: writing `max_dec_frame_buffering=1` / `max_num_reorder_frames=0` into the SPS (the decoder
   uses `MaxDPBNumber = 16`), vendor-specific low-latency keys, not rendering the output to a surface,
   inserting empty AUD packets between frames.
3. What worked: **shortening the frame interval.** The tablet decodes 1280×752 at 120 fps without dropping frames.
   The load stays low because unchanged regions are encoded as near-zero-byte "skip" blocks.
   → In the Phase 1 encoder: a few fast "pusher" frames after each screen change, no frames at all while the screen is static.
4. The tablet's Wi-Fi is 2.4 GHz, idle ping ~80–115 ms (PC→router 10 ms). A high-performance Wi-Fi lock
   helps a little. Real network latency will be measured with ping/pong in the Phase 1 protocol; USB comes
   first for this device.
5. The tablet's kernel supports `f_accessory` (AOA) and `f_rndis` (USB tethering): both USB paths are open.

Tryout: `tools/faz0.ps1` (sends from the PC), `tools/sayac.html` (for measuring end-to-end latency by photo).

## Phase 1 – First release: Mirror ✅ (0.1.0)

Design rule: **keep system load low.** The image never goes down to the CPU (DXGI → GPU video processor → hardware
encoder), nothing is encoded/sent while the screen doesn't change, the cursor is drawn locally on the tablet.
Building doesn't need Visual Studio: MinGW-w64 (GCC) is enough.

- [x] Protocol v1: length-prefixed frames, handshake, frame acknowledgement, ping/pong → [PROTOCOL.md](PROTOCOL.md)
- [x] Windows server (C++, single 350 KB exe): DXGI capture → scale + NV12 on the GPU → Media Foundation
      hardware encoder (one code path for NVIDIA/AMD/Intel), low-latency VBR
- [x] Pusher frames (Phase 0 finding 3) + acknowledgement-based flow control (latency doesn't accumulate)
- [x] Cursor sent separately, drawn on the tablet (cursor movement needs no encoding)
- [x] USB-ADB: adb forward, waking the tablet, launching the app; Wi-Fi: `--ip`
- [x] Touch → mouse (single finger)
- [x] Automatic discovery on Wi-Fi (UDP broadcast on every network interface; the USB tethering network is scanned too — not yet tried on a real device)
- [x] Automatic mode: USB first, otherwise Wi-Fi; reconnect on disconnect (on USB it also restarts the tablet app)
- [x] Pairing + encryption: ephemeral ECDH P-256; on Wi-Fi, the first pairing requires confirming the same 6-digit
      code on both sides, on USB no confirmation; Wi-Fi data is ChaCha20-Poly1305 (verified with RFC 8439 test vectors,
      `ekranlink-test.exe`), USB is unencrypted. Details: [PROTOCOL.md](PROTOCOL.md)
- [x] The pairing prompt on the PC closes by itself when the tablet rejects
- [x] "Forget pairings" button on the tablet (visible only while not connected)
- [x] Notification area (tray) app `EkranLink.exe`: status, connection type, display selection, Start with Windows, log
- [x] Separate downloads (0.3.1): the PC doesn't install/offer the app to the tablet. `tools\paketle.ps1` →
      `dist\EkranLink-<sürüm>-windows.zip` (exes + adb) and `dist\EkranLink-<sürüm>-android.apk`.
      If the app is missing on USB, the PC says so with a notification; on a protocol version mismatch the tablet sends
      `Incompatible`, and both sides show which app needs updating
- [x] Settings window (0.4.0): Direct2D, follows Windows' light/dark theme and DPI, uses resources only while
      open. Tray left click opens the window, right click a short menu; launching a second time brings the window to front
- [x] Turkish / English: `i18n.h` on the PC (System / Türkçe / English), `values` / `values-tr` on Android.
      The service's notifications are carried as codes; the tray shows them in its own language
- [x] Persistent monitor orientation: Automatic (follows the tablet) or fixed; changes made in Windows display settings are
      saved too (`Orientation`, `LastRotation`)
- [x] Phone layout: compact layout when the short edge is < 520dp, scrolling welcome screen if needed, notch insets
- [x] Live settings (0.4.1): changing a setting doesn't restart the stream (`Streamer::update`). Touch, performance,
      auto-connect, orientation, resolution, Extend ↔ Only this apply instantly; Mirror ↔ Extend and the mirrored
      display only by rebuilding the video pipeline (the connection and the tablet decoder survive). When leaving
      Only this, the tablet's current resolution/orientation is kept
- [x] Resolution selection (0.4.2): the same modes Windows offers for that monitor, in the same format
      ("1600 × 940", the device's own marked "(Recommended)"); saved per device (`Cozunurluk-<cihaz>`), including
      changes made from Windows
- [x] Paged settings window (0.4.2): left menu (Status, Connection, Display, Touch and performance, General), no
      scrolling; switch with 1–5 / ↑↓ / Ctrl+Tab. Status page: connection, mode, display, latency / data / frame rate
      (in service mode transferred only while the window is open), warnings that stay until resolved, shortcut to Windows display settings
- [x] Orientation / resolution are also written to Windows' registry (0.4.3, not in Only this): the monitor opens directly
      in the right mode on the next connection, no extra mode change
- [x] With multiple adb devices plugged in: the prepared one is chosen, otherwise the one with EkranLink installed; all adb commands go to
      that device with `-s` (full multi-device support is on the roadmap)
- [x] Reliable Wi-Fi discovery: socket per interface, 3 retries, direct query to known tablet addresses
      (tested with broadcast blocked by the firewall: connected in 1.2 s). The tablet retries until the UDP/TCP port is free
- [x] Persistent APK release key (outside the repo; v1+v2+v3 signing, v1 required for Android 4.x)
- [ ] Do USB polling with Windows device notifications instead of `adb get-state`
- [ ] Software encoder fallback (PCs without a hardware encoder)

Measurement (S6000, USB, RTX 2060):

| Situation | PC→drawn on tablet | PC CPU (single core) |
|---|---|---|
| Continuous motion (~116 fps) | ~21 ms | ~10% |
| Sparse changes (~10/s) | ~38 ms (24 ms of it pusher frames) | ~2% |
| Static screen | — (no frames) | ~0% |
| No tablet, searching | — | 0.000% (9.7 MB memory) |

Encryption choice (S6000, Android 4.2, single core, 16 KB chunks):

| Algorithm | Provider | Speed |
|---|---|---|
| AES-128-CTR | BouncyCastle (pure Java) | 1.4 MB/s |
| HMAC-SHA256 | BouncyCastle (pure Java) | 1.7 MB/s |
| ChaCha20 | EkranLink (hand-unrolled for Dalvik) | 15.4 MB/s |
| Poly1305 | EkranLink | 18.1 MB/s |

Tablet app CPU during continuous motion (116 frames/s, 6.6 Mbit/s): USB 33%, Wi-Fi AES+HMAC 104%,
Wi-Fi ChaCha20-Poly1305 48%. Note: the Dalvik JIT doesn't inline small methods (e.g. `Integer.rotateLeft`);
unrolling them by hand in hot loops gave a ~7× speedup.
- [ ] Reduce per-frame copies and acknowledgement messages on the tablet side (bring down the 33% on USB)
## Phase 2 – Extend (in progress)

Ready-made drivers for the virtual monitor were evaluated: Virtual Display Driver (MIT, signed, but reloads
the whole driver to add a monitor), Parsec VDD (closed source, not redistributable), SudoVDA (MIT, has a per-tablet
add/remove API but no signed release), HDMI dummy plug (driverless; works today with `--ekran N`).
Choice: **our own driver based on SudoVDA** → [driver/](../driver/README.md), built on this PC with
MSVC + WDK and signed with a local certificate.

- [x] EkranLink virtual display driver (IddCx/UMDF): per-tablet plug/unplug, stable monitor ID
      (Windows remembers the layout), physical size in the EDID, 3 s watchdog (the monitor is removed even if the program crashes)
- [x] Install/uninstall: `driver\kur.ps1` (local signing that deletes the private key right after signing), `driver\kaldir.ps1`
- [x] PC program: when a tablet connects, create a 1280×752 monitor → capture it; remove it on disconnect; mirror if there's no driver
- [x] Tray menu: Display → Extend / Mirror
- [x] Multi-touch: presented to Windows as a real touchscreen (InjectTouchInput, 10 fingers; scrolling, zooming,
      press-and-hold right click, touch keyboard). "Mouse" mode can be selected from the tray menu. Pressure-sensitive pen is out of scope.
- [x] Hardware cursor: the cursor is drawn locally on the tablet (done in Phase 1)
- [x] Rotation: when the tablet is held upright the virtual monitor becomes portrait in Windows (DMDO_90/270). The tablet's screen,
      stream size and decoder never change — Windows already delivers the rotated image in panel orientation; only
      the cursor and touch are transformed. The tablet's "auto-rotate" lock is respected; the accelerometer is on only while connected and
      at the slowest rate (200 ms). The connection no longer drops on orientation/resolution/layout changes.
- [x] Tablet info overlay redone (welcome screen, stats card, pairing confirmation) and rotates with how the tablet
      is held; since Android dialogs can't be rotated, the pairing and "forget pairings" confirmations live in this overlay too
- [x] In Mirror, a PC monitor rotated to portrait is straightened in the video processor (stays sideways on GPUs that don't support it;
      touch and cursor are correct either way)
- [x] Android 16+ large screens (600dp+): with targetSdk 36 the landscape lock is ignored and the window can open in portrait.
      The image is rotated in the decoder (MediaFormat KEY_ROTATION; done by the GPU, no extra load), the cursor on the canvas, touch
      by hand at the same angle; the display is always reported to the PC as landscape and the monitor turns portrait as with a landscape-locked app.
      Measured (Android 16 emulator, sw720dp): in all four orientations (including reverse portrait) touch ±1 px, cursor under the finger
- [ ] Take the image directly from the driver instead of DXGI capture (one fewer copy, sign-in screen)
- [x] Open source on GitHub (github.com/meg55/EkranLink): driver license texts, contributing and security documents
- [x] Automated builds (GitHub Actions, `.github/workflows/derle.yml`): every push builds Android + Windows,
      runs the encryption tests, builds the driver (unsigned); on a `v*` tag the packages become a draft release (publishing is
      manual). The compiler matches the local one (WinLibs GCC 16.1.0, verified by hash); actions pinned to commit hashes
- [x] Windows installer (Inno Setup, `host-windows/installer/EkranLink.iss`; Turkish / English): Program
      Files\EkranLink, Start menu, uninstall from the *Apps* list, optional Start with Windows
      (in the registry of the user who ran the installer). On update, a running EkranLink is closed cleanly (WM_CLOSE); if the service
      is installed it is stopped and reinstalled with the new version; on a silent update (winget) the tray is reopened in the
      background. Uninstalling removes the service and asks whether to delete settings and pairings. Turning off service mode doesn't remove the
      installed program. The zip remains as the portable version. Tested (PC with service mode on): install over existing,
      service off / on, silent uninstall, reinstall; no restart needed. The driver isn't in the package yet
- [x] Security review and protocol v3 (0.5.0). Previously USB was trusted without confirmation: an unprivileged
      program on the PC (the adb server is open to everyone) or an app on the phone could pose as a USB device
      and, in service mode, touch the lock / UAC screen. Now:
      - A new device is confirmed once with the same code on both screens, over USB too. A paired device proves
        its key on every connection (the PC's key confirmation + encrypted Hello).
      - The pairing code is protected by a commitment to the PC's random value: a man in the middle cannot make
        the codes match (previously ~10⁶ tries, a few seconds, could find a match).
      - Device→PC (including touch) is always encrypted; PC→device video is unencrypted only over USB (old
        tablets). adb over the network does not count as local.
      - An unpaired device found on the network cannot open a prompt (the user clicks *Connect*); the PC prompt
        defaults to "No", one prompt at a time, no automatic prompt for 1 minute after a rejection. A connection
        that fails authentication does not delete the pairing.
      - Before authentication, a 4 KB frame limit and a timeout; device names and IDs are sanitized; at most 16
        devices on Wi-Fi.
      - Android: on 4.1–4.3 random numbers come from /dev/urandom (old SecureRandom seeding bug), keys via BC;
        AutoStartReceiver only open to adb (DUMP permission); raw H.264 only on a local connection; NAL, SPS and
        cursor-shape limits; excluded from backup and device transfer.
      - Windows: a pre-created ProgramData\EkranLink folder cannot be hijacked (ownership is taken), the log
        written by SYSTEM does not follow links / hard links; the service only runs its own adb; adb serial
        numbers are validated; DLLs only from System32 and the app folder; pairing keys are removed from the
        user's registry once moved to the service.
      Known limitations: SECURITY.md
- [x] Real-device tests (0.5.1, S6000): touch 0 px (USB and Wi-Fi), modes, disconnect / connect, background,
      display power sync, resolution changed from Windows is remembered, press and hold for right click, lock
      screen in *Only this*, after a PC restart the service is ready on the lock screen. With continuous motion
      (counter page) 52–53 fps, PC→draw ~37 ms, decode ~26 ms; PC CPU avg 5.7%; tablet app 6%, decoder 2% (v3's
      device→PC encryption added no load). Network defences (raw mode rejected from the network, idle connection
      closed after 15 s, 1 MB handshake frame rejected) and that an unknown device on the network cannot open a
      prompt were verified on the real tablet. Found and fixed: (1) after an unexpected drop (e.g. the adb server
      restarted) there was no reconnect while auto-connect was off: now retried for 1 minute, and these retries
      cannot open a pairing prompt; (2) the end of a fast swipe was lost (Windows ignores the position in the
      "up" frame): if the lift position differs, the contact is moved there first
- [ ] Distribution signing (SignPath Foundation or EV + Microsoft) — for now the driver can only be installed on the PC it was built on

Measurement (S6000, USB, Extend 1280×752): PC→drawn on tablet ~27 ms, PC CPU 0.3% with a static screen.
### Resource usage optimization

Measurement: S6000 + RTX 2060, USB, Mirror, continuous motion on screen (counter page). PC RAM = "private working set" in
Task Manager; tablet RAM = `dumpsys meminfo` PSS.

| | Before | After (balanced) |
|---|---|---|
| PC CPU (single core) | 14.4% | 4.3% |
| PC RAM | 25 MB | 25 MB |
| Tablet app | 39.4% | 19.2% |
| Hardware decoder on tablet (mediaserver) | 15.0% | 10.6% |
| Tablet RAM | 28 MB | 26 MB |
| Latency during motion | ~20 ms | ~36 ms |
| Static screen (Extend) | — | PC 0.6% · tablet 2% + 0.9% |

Done:
- Frame rate capped at 60 (the tablet's display): frames that can't be shown aren't encoded/decoded, even if the PC display is 120/144 Hz.
- In "balanced" mode pusher frames are sent only when motion stops; in "low latency" mode (tray → Performance)
  they're also inserted between frames: ~32 ms, the tablet works ~1.5× harder.
- Tablet: fast path for P-frames (no scanning, no intermediate copy), buffered sending, batched frame acknowledgements
  (every 3rd frame; immediately when the decoder drains), the decoder output loop wakes less often when idle, the hidden stats
  overlay does no work.
- PC: D3D11 driver threading optimization off (two fewer threads).
- The "private bytes" on the PC (~93 MB) are largely virtual space the NVIDIA driver reserves but doesn't use; real
  RAM ~25 MB (D3D11 device ~60 MB virtual, Media Foundation encoder ~32 MB virtual). Diagnostics: `EKRAN_BELLEK=1`.
## Phase 3 – Real external display

- [ ] Plug-and-play via AOA (no settings on the tablet, the app opens by itself)
- [ ] Our own IddCx driver: per-device plug/unplug, device-specific EDID (Windows remembers the layout),
      physical size → correct DPI
- [x] Windows service (tray → "Start before sign-in"): starts at boot, before sign-in. The service
      (Session 0) starts an agent with SYSTEM rights in the active console session; the agent follows the input desktop (Default ↔
      Winlogon) to capture the lock screen, password screen and Ctrl+Alt+Del, and forwards touch there.
      Settings/status live in HKLM (the tray is only a UI), pairings in a section only SYSTEM can read;
      no new tablet can be paired while locked. The user's adb key is carried over (the tablet doesn't ask for permission again).
      Measured (S6000, USB, tablet as primary display): the lock screen appears on the tablet, swiping up opens the password
      screen, touch is forwarded. Note: Windows draws the lock screen only on the **primary** monitor (on a headless mini
      PC the tablet is the primary display anyway; on a desktop you need "Only this" or to make the tablet the primary display).
      The UAC secure desktop goes through the same path but couldn't be tested because this PC elevates without a UAC prompt
- [x] Auto-connect is configured on both sides: PC (tray → Connection → Connect automatically / Connect now) and
      tablet (welcome screen → Open automatically when plugged into a computer); with both on, it just happens when plugged in
- [x] Background: when the tablet app is sent to the background, the connection and the monitor in Windows persist (foreground service +
      notification); while in the background the PC produces no image (agent 0% CPU), and on returning to the foreground it resumes instantly with a keyframe.
      To disconnect: "Disconnect" in the notification or the stats card
- [x] USB plug notification (CM_Register_Notification, ADB interface): adb is never run while no tablet is
      plugged in; installation check once per device
- [ ] Wi-Fi before sign-in: the tablet app must be open and paired (as today); for the first boot
      USB is recommended for connecting the tablet
- [x] Only this mode (tray → Display → Only this, `--tek-ekran`): when the tablet connects the other monitors are turned off and
      the tablet becomes the primary display; on disconnect the previous layout is restored. The layout isn't saved to Windows' database:
      if the program crashes the driver's watchdog removes the monitor and Windows returns to the saved (tablet-less) layout
- [x] Sleep sync: when Windows turns off the displays (power setting notification, no polling) the tablet goes dark and turns off
      its screen with its own timeout; the connection and Wi-Fi stay up. When Windows turns the display on, the tablet lights up too.
      On USB the PC turns off the tablet's screen immediately via adb (with "stay awake" on in developer options the
      timeout never fires). Touching the tablet while dark wakes Windows.
      Measured (S6000, USB): the tablet screen turns off in ~3 s, moving the mouse brings the image back in ~4 s, the connection doesn't drop
- [x] Multiple devices at once: each with its own session and its own virtual monitor; a separate adb
      port per device on USB; touches in one process-wide table (every frame sent to Windows contains all devices'
      fingers); the Only this layout is shared (EkranLink monitors stay on, the last one to leave restores it); resolution and
      last orientation per device. *Connect* / *Disconnect* per device in the window; a disconnected device isn't auto-connected
      until the user reconnects it or the device is unplugged. The same device on USB and Wi-Fi is a single entry.
      Measured (S6000 + Android 16 emulator, USB, service mode): two monitors, disconnecting / connecting each separately
      (~3 s), disconnect all, Only this ↔ Extend (layout restored exactly), touching one while scrolling
      the other (±1 px)

## Phase 4 – Every device

- [ ] iOS app (Swift, VideoToolbox)
- [ ] Web client (JPEG mode for old browsers, H.264 mode for new browsers)
- [ ] Lightweight mode: only changed regions, as JPEG
- [ ] Audio forwarding
- [ ] E-ink mode

## Phase 5 – Extras

- [ ] System panel (CPU/GPU temperature, clock, media) – without a video stream, drawn locally
- [ ] Shortcut panel (Stream Deck-like)
- [ ] Battery safety: temperature/charge monitoring and warnings during constant charging
- [ ] Video wall, clipboard sharing, touchpad mode
- [ ] Experimental HDMI mode: USB HDMI capture stick + OTG → a real monitor, BIOS included
- [ ] Linux / macOS server
