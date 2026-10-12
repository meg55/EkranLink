**English** · [Türkçe](PROTOCOL.tr.md)

# EkranLink protocol v3

A single TCP connection, default port **27183**. The connection is always opened by the PC (Android 4.x has no
`adb reverse`): over USB via `adb forward tcp:27183 tcp:27183`, over Wi-Fi to the tablet's IP.

Unless stated otherwise, all numbers are **big-endian**. `str` = `u16 length` + UTF-8 bytes.
Direction markers: `P` = PC, `T` = tablet/phone (the Android device); `P→T` = PC→device, `T→P` = device→PC.

## Discovery (UDP, port 27183)

The PC asks on the broadcast address of every network interface (e.g. 192.168.1.255, or 192.168.42.255 with USB tethering):

- Query (P→T): `"EKRN?"` + `u16 version`
- Reply (T→P, to the sender's address): `"EKRN!"` + `u16 version, u16 tcpPort, str name, str id`

While idle, the tablet sends no packets at all; it only replies when a query arrives.

The query is sent from every interface through a separate socket bound to that interface's address: on a PC
connected to the same network with two adapters (Ethernet + Wi-Fi), a broadcast from an unbound socket leaves only through the adapter chosen by routing.
Wi-Fi broadcasts are sent unacknowledged and a tablet in power-saving mode may miss them; therefore the query is repeated 3 times
within 1 s, and the addresses of tablets previously connected over Wi-Fi (at most 4, `Tabletler` in the pairing
store) are also queried directly. A direct packet is delivered by the access point with acknowledgement and retransmission.

## Frame

```
u32 bodyLength
body = u8 type + payload
[16-byte tag]      only on an encrypted connection
```

On an encrypted connection the body is encrypted in place with **ChaCha20-Poly1305** (RFC 8439, empty associated data).
Nonce = `00 00 00 00` + `sequence number (u64, little-endian)`; each direction has its own key and its own sequence
number starting from 0, and the sequence number is incremented on every frame (replaying / reordering is useless).
If the tag does not verify, the connection is dropped immediately.

## Handshake and security

```
P→T  "EKRN" + u16 version (3)                                                raw
P→T  PcHello   u8 flags, 16 pcId, 65 ecdhP, 32 commit, str pcName           unencrypted frame
T→P  TabletKey 65 ecdhT, 16 nonceT, str deviceId, str deviceName, u8 knowsThisPc
P→T  Mode      u8 paired, u8 plainVideo, 16 nonceP
     ── from this point on both directions are encrypted ──
     if paired:      P→T PairResult 1 (key confirmation)
     if not paired:  P→T PairResult u8 accept, T→P PairResult u8 accept
T→P  Hello ...
     ── if plainVideo = 1, PC→tablet is unencrypted from here on; tablet→PC always stays encrypted ──
```

If the tablet does not support the PC's version, it sends `Incompatible` (24) instead of `TabletKey`:
`u16 tabletVersion, str appVersion` (unencrypted frame) and closes the connection. Both sides
tell the user which app needs to be updated (the PC does not install the app on the tablet; the two are downloaded separately).

- `ecdh*`: ephemeral ECDH P-256 public key (`04 || X || Y`), a new one is generated for every connection.
  The tablet verifies that the received point lies on the curve; Windows CNG verifies it on import.
- `commit` = SHA-256(nonceP). The PC commits to its random value first and reveals it in `Mode`; the tablet
  closes the connection if it does not match. A man in the middle therefore cannot pick its own values after
  seeing the PC's and make both screens show the same code (without the commitment, ~10⁶ tries — a few
  seconds — would find a match).
- `Z` = ECDH shared secret (X coordinate, 32 bytes, big-endian).
- `H` = SHA-256(ecdhP ‖ ecdhT ‖ nonceP ‖ nonceT ‖ pcId ‖ deviceId)
- `K` = pairing key (if paired), otherwise 32 zero bytes
- `PRK` = HMAC-SHA256(key = H, data = Z ‖ K)
- Session keys: `HMAC(PRK, "ekran p2t key")` (PC→tablet), `HMAC(PRK, "ekran t2p key")` (tablet→PC)

Before authentication (until the keys are set up) both sides accept frames of at most 4096 bytes.

**Paired device:** right after `Mode` the PC sends an encrypted `PairResult 1`; if the tablet can decrypt it,
the PC has proven it knows the pairing key. The tablet proves the same with its encrypted `Hello`. If the key
does not match, the connection is closed; **records are not deleted** (an unauthenticated peer must not be
able to erase a pairing). If the device really forgot the pairing, it says `knowsThisPc = 0` and pairing is
needed again.

**Pairing** (`paired = 0`), the same over USB and Wi-Fi: both sides display a 6-digit code derived from the
first 4 bytes of `HMAC(PRK, "ekran sas")` (big-endian, mod 1 000 000); if both accept, the new pairing key
`HMAC(PRK, "ekran pair")` is stored. Each device is paired once. "Reject" on the tablet closes the
connection; the PC also counts this as a rejection and opens no new prompt on its own for 1 minute. On the
PC the default button is "No" and only one prompt is shown at a time.

The PC only opens the prompt for an unpaired device if the user clicked *Connect* in the window or the device
was plugged in with a USB cable. An unpaired device found on the network appears in the list but cannot open
a prompt.

**USB** (adb forward; on the tablet the connection arrives from 127.0.0.1): authentication is the same as
over Wi-Fi (another app on the same device can also connect to 127.0.0.1, and on the PC the adb server is
open to every local program). Only to spare old tablets' CPUs, `plainVideo = 1` is chosen: after `Hello`,
PC→tablet frames are sent unencrypted; tablet→PC (including touch) stays encrypted and authenticated. The
tablet rejects `plainVideo = 1` from a non-local connection. adb over the network (`adb connect`, wireless
debugging) does not count as local, and video is encrypted there too.

## Messages

| Type | Name | Direction | Payload |
|---|---|---|---|
| 1 | Hello | T→P | `u16 version, u16 surfaceWidth, u16 surfaceHeight, u16 screenWidth, u16 screenHeight, u16 dpi, u16 sdk, str model, str decoder` |
| 2 | Config | P→T | `u16 streamWidth, u16 streamHeight, u8 codec(1=H.264), u16 sourceWidth, u16 sourceHeight, u16 contentX, u16 contentY, u16 contentWidth, u16 contentHeight` |
| 3 | Video | P→T | `u64 pts(µs, PC clock), u8 flags(bit0 key frame, bit1 pusher frame), ... Annex-B data of a single frame` |
| 4 | Ping | P→T | `u64 time` |
| 5 | Pong | T→P | `u64 time` (the value from the Ping, unchanged) |
| 6 | FrameAck | T→P | `u64 pts, u32 decodeTime(µs), u16 count` — the last rendered frame; `count` is the number of frames this acknowledgement covers (every 3rd frame while motion continues, sent immediately when the decoder drains; otherwise 1) |
| 7 | CursorShape | P→T | `u16 width, u16 height, u16 hotspotX, u16 hotspotY, width×height × u32 ARGB` |
| 8 | CursorPos | P→T | `s16 x, s16 y, u8 visible` — top-left of the cursor, in source image pixels |
| 10 | Touch | T→P | `u8 action(0 down,1 move,2 up,3 cancel), u8 finger, u16 x, u16 y, u16 pressure(0–1024)` — stream pixels; each finger with its own `finger` id, `cancel` cancels all fingers |
| 11 | Orientation | T→P | `u16 degrees` (0, 90, 270) — how the tablet is held, clockwise relative to the landscape screen. Sent only while the tablet's auto-rotate is on, on connect and when the orientation changes |
| 12 | DisplayPower | P→T | `u8 on` — Windows turned the displays off (0) / on (1). The tablet dims and lets its screen turn off via its own timeout; the connection stays up. When turned on, the tablet lights its screen |
| 13 | KeyRequest | T→P | (empty) — the tablet re-created its decoder (its surface was destroyed and came back); the PC sends a key frame and resets the acknowledgement counter |
| 14 | VideoVisible | T→P | `u8 foreground` — the tablet app was sent to the background (0) / came to the foreground (1). While in the background the PC stops capturing and encoding, the connection stays up; on returning to the foreground the tablet sends `KeyRequest`. While in the background the PC does not turn off the tablet's screen |
| 20–24 | PcHello, TabletKey, Mode, PairResult, Incompatible | | above |

If the first 4 bytes are not `"EKRN"`, the tablet treats the stream as raw H.264 Annex-B (diagnostic mode, `tools/faz0.ps1`).

### Content rectangle

The source screen (e.g. 1920×1080) is fitted into the stream (e.g. 1280×752) preserving its aspect ratio; the remainder is black.
`content*` gives this placement in stream pixels. The tablet positions/scales the cursor according to it,
and the PC maps touches back to the desktop using its inverse.

### Auto-start over USB

The PC asks the tablet via adb: `am broadcast -f 32 -n org.ekranlink.android/.AutoStartReceiver`. The result code
is the tablet's decision: **1** allowed (the PC launches the app with `am start`; since Android 10+ does not allow turning on the screen
from the background, it is the PC that launches it), **2** refused ("open automatically when plugged into the computer" is off, or
the user disconnected from the tablet — until the cable has been unplugged for 30 s or the app is opened manually),
**3** the app is already open (the PC connects). The PC also has an "auto-connect" setting: if both are on, the tablet
automatically becomes a display when plugged in.

The result data is `kimlik|ad` (i.e. `id|name`, e.g. `9774d56d682e549c|LENOVO Vodafone Smart Tab III 10`): the id is the same one sent in the handshake and
in Wi-Fi discovery. While looking for devices the PC also asks without opening the app (`--ez kimlik true`; this
query does not extend the "disconnected" hold-off): it distinguishes multiple devices and does not list the same device twice over USB and Wi-Fi
(if it is present on both, USB; if one of them is connected, that one).

### Multiple devices

Each device is a separate session (separate TCP connection, encoder and, in extended-display mode, separate virtual monitor). Over USB
a separate local port is opened on the PC for each device (`adb -s SERIAL forward tcp:0 tcp:27183`; adb picks a free
port). In single-display mode all of EkranLink's monitors stay on; only the others are turned off.

### Rotation

The tablet app always stays in landscape. When the tablet is held upright it sends `Orientation`; in extended-display mode the PC rotates the virtual
monitor (90 → DMDO_90, 270 → DMDO_270). Because Windows delivers the image of a rotated display in the panel's own
orientation, the stream, `Config` and the decoder do not change: `sourceWidth×sourceHeight` is always the size of this
image. The PC sends the cursor image and position converted to this image's orientation, and applies incoming touches to the desktop
after converting them back. In mirror mode the PC's real monitor is not rotated.

### Pusher frames and flow control

Some hardware decoders (e.g. MediaTek) hold a fixed ~3 frames internally; if no new frame arrives, the last image
never reaches the screen. Therefore, after the screen changes, the PC sends a few **pusher frames**
(unchanged image, almost zero bytes) at short intervals. While the screen is static no frames are sent.
Real frames have even pts values, pusher frames have odd ones.

The PC limits the number of frames it has sent but not yet received a `FrameAck` for. When the limit is reached it does not encode new frames,
it only remembers that "the screen changed"; when an acknowledgement arrives it encodes the most recent image. This way, on a slow
connection, latency does not accumulate.
