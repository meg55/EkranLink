**English** · [Türkçe](README.tr.md)

# EkranLink virtual display driver

An IddCx (Indirect Display) driver that adds a real monitor to Windows at the device's own resolution when a
device connects, and removes it when the device leaves. It runs in user mode (UMDF 2); no code is loaded into
the kernel.

## Origin and license

This directory is **licensed separately from the rest of EkranLink (MIT)**; the full license texts are in
[LICENSE](LICENSE). The driver is a separate program; EkranLink talks to it only through device I/O control
codes (IOCTL). Because parts of it come from the Microsoft sample, the source in this directory as a whole is
distributed under the **MS-PL**.

- Microsoft's [Indirect Display Driver sample](https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay) — MS-PL.
- Built on that: [Virtual-Display-Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)
  (IddSampleDriver) — MIT.
- Built on that: [SudoVDA](https://github.com/SudoMaker/SudoVDA) (SudoMaker) — *"MIT and CC0 or Public
  Domain … choose the least restrictive option"*.
- EkranLink changes (MIT): EkranLink-specific hardware ID / interface GUID / registry path (so it does not
  clash on machines with SudoVDA or Apollo installed), bounds checks and terminated strings for external input
  (the 14-byte fields copied into the EDID), the device's physical size written into the EDID (Windows suggests
  scaling from it), "EKR" manufacturer code, removal of an unnecessary high process priority, missing `break`s,
  a cursor event leak, device access limited to SYSTEM and administrators, a software device that exists only
  while in use (hardware ID `EkranLinkVD`), running from the driver store, and a thread-safe monitor list and
  watchdog.

## Commands (`src/ekranvd-ioctl.h`)

| Command | What it does |
|---|---|
| `IOCTL_ADD_VIRTUAL_DISPLAY` | Width, height, refresh rate, monitor GUID, name, serial number, physical size (mm) → adds a monitor |
| `IOCTL_REMOVE_VIRTUAL_DISPLAY` | Removes the monitor with the given GUID |
| `IOCTL_DRIVER_PING` | Feeds the watchdog. If the PC program is silent for 3 s, all virtual monitors are removed (a crash never leaves an "invisible screen" behind) |

## When the device exists

The driver is not tied to a permanent device. When a device connects in *Extend* or *Only this*, EkranLink
(the service's agent, running as SYSTEM) creates a software device (`SwDeviceCreate`); Windows loads the
driver and the monitor appears. 15 seconds after the last monitor leaves, or as soon as EkranLink exits or
crashes, Windows removes the device, so it is not listed in Device Manager while unused (only under
*View > Show hidden devices*). Creating a device needs administrator rights, so *Extend* and *Only this* need
the EkranLink service; without it EkranLink mirrors. Versions up to 0.5.3 installed a permanent root device;
`kur.ps1` removes it.

A device gets the same monitor GUID every time it connects (derived from the device ID), so Windows remembers
each device's display arrangement and scaling separately.

## Build, install, uninstall

Requirements: Visual Studio Build Tools (MSVC x64) + WDK 10.0.26100. The MSBuild driver toolset is not needed.

```powershell
.\build.ps1      # out\EkranLinkVD.dll + ekranlinkvd.inf
.\kur.ps1        # administrator: sign + trust + add to the driver store; creates no device (needs host-windows\build\ekranlink-surucu.exe)
.\kaldir.ps1     # administrator: removes the device records, driver package, certificate and driver settings (pairings stay)
```

**Signing:** as a UMDF driver it does not need Windows test mode. `kur.ps1` creates a new code-signing
certificate every time, signs the driver, adds only the public part of the certificate to the "Trusted Root"
and "Trusted Publishers" stores and **deletes the private key immediately**, so nothing else can ever be
signed with it. This method is only suitable for the machine the driver was built on. Distribution needs a
code-signing certificate that Windows trusts: SignPath Foundation (free for open source) or a commercial
certificate (e.g. Certum Open Source). Being a user-mode driver, it does not need Microsoft's attestation
signing (EV + Partner Center) that kernel drivers require.
