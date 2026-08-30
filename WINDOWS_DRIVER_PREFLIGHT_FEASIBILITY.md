# Windows driver preflight feasibility

Status: investigation and recommendation. No implementation is included.

## Decision

A central Windows driver preflight owned by the self-managed Sigrok package is
technically feasible. It can enumerate *currently attached* devices, classify
their PnP binding against a curated package manifest, gather all missing
supported driver packages, and invoke one elevated installer process. This
keeps all driver responsibility out of the Extension and produces only the
normal Windows UAC consent UI.

It is not feasible or safe to treat every enabled libsigrok hardware driver as
having a known, installable Windows driver. The current build metadata only
describes build-time user-space dependencies such as `libusb` and
`libserialport`; it does not contain a VID/PID/interface to signed-INF mapping
or the permission to redistribute vendor drivers. A blind "install all common
drivers" action would be unreliable and can replace a working vendor binding.

Recommended product behavior:

```text
first device scan in a libsigrok context
  -> central Windows PnP preflight (once per process/context)
  -> find present devices matching an explicit supported-driver manifest
  -> ignore devices with a healthy acceptable binding
  -> collect only manifest entries whose matching device has no usable binding
  -> one ShellExecuteExW(..., "runas", privileged helper)
  -> helper validates and installs only the collected signed packages
  -> re-enumerate PnP devices
  -> execute the requested normal libsigrok scans
```

The preflight must never install a package for an absent device, replace an
existing non-missing binding, infer an INF from `libusb`, or attempt a second
UAC elevation in the same process after cancellation/failure.

## Clarification: generic transport versus device driver

The intuition that most entries are "generic USB/serial drivers" is directionally
right, but the layers must not be conflated:

1. `libusb`, `libftdi`, `hidapi`, and `libserialport` are user-space libraries
   shipped with Sigrok. They implement transfers or port access; they are not
   Windows kernel driver packages.
2. Windows still assigns every physical USB interface to a PnP function driver.
   This can be an inbox class driver (HID, USB hub, CDC/`usbser.sys`), a
   generic WinUSB/libusbK binding, or a vendor driver. The user-space library
   does not determine which one is correct.
3. A device protocol implemented in a Sigrok backend is not evidence that the
   corresponding chip driver can be replaced by Sigrok source. Firmware and
   driver binding are separate from the protocol commands.

For the current package, the strongest classification is:

| Current case | Windows driver situation | Confidence |
| --- | --- | --- |
| `wch-ch32h417` / CH375 (`VID_1A86&PID_5537`) | WCH private `CH375_A64` kernel service plus `CH375DLL64.DLL`; the service is not supplied by libusb | Confirmed |
| Raw USB backends such as DSLogic and FX2-based drivers | Usually a generic WinUSB/libusbK or vendor USB binding; exact choice is device/interface-specific | Confirmed category, per-device binding not yet audited |
| FTDI backends (`libftdi`) | Either an FTDI VCP/D2XX stack for a COM-port path or a generic USB binding for libftdi raw access; changing the binding can change behavior | Confirmed as multiple possible modes |
| HIDAPI backends | Usually Windows inbox HID (`hidclass.sys`); no extra package in the normal case | Likely, must handle nonstandard/rebound cases |
| CDC/USB-serial backends | CDC may use inbox `usbser.sys`; CP210x, CH34x, FTDI and custom VID/PID devices can require vendor or generic USB-serial bindings | Mixed |

Thus CH375 is the only *currently confirmed* backend in this checkout that
explicitly loads a WCH private DLL and requires a matching private kernel
service. It is not the only possible source of a missing Windows driver:

- `README.devices` documents GW Instek VCP devices using a CP210x chip with
  VID/PID `2184:0030`; it says to install the vendor Windows driver to obtain a
  COM port. This is a chip/vendor driver exception even though the Sigrok
  backend only declares `serial_comm`.
- FTDI devices can require the FTDI VCP driver when used as serial ports, while
  the same family of chips may be deliberately assigned WinUSB/libusbK for a
  libftdi backend.
- A libusb backend may fail with no usable binding even though no proprietary
  DLL is involved. The remedy is normally a generic binding assignment, not a
  Sigrok protocol rewrite.
- Some devices expose multiple interfaces or change VID/PID after firmware
  upload. One interface can be inbox-supported while another requires a
  vendor/generic binding.

Therefore the accurate product statement is: **CH375 is the primary and only
confirmed private-driver case; all other devices must be treated as
device-specific until their Windows binding is verified.** The global
installer should initially package CH375 only, then add generic or vendor
packages one audited VID/PID/interface at a time.

## Why the current package cannot infer this automatically

The current x64 build has 83 enabled source drivers. Its build manifest counts
21 drivers depending on `libusb`, 5 on `libftdi`, 1 on `libhidapi`, 1 on
`libserialport`, and 40 on `serial_comm` (some drivers belong to more than one
group). These are not Windows kernel driver identifiers:

| libsigrok dependency | What it means | Why it is insufficient for installation |
| --- | --- | --- |
| `libusb` | User-space application API for raw USB access | On Windows, the device may need WinUSB, libusbK, a vendor driver, or an existing vendor binding. The correct choice is device/interface-specific. |
| `libftdi` | User-space API for FTDI chips | Device-specific VID/PID may use an FTDI VCP/D2XX binding or a libusb-compatible binding. Changing it can remove a COM port or break vendor software. |
| `libhidapi` | User-space HID API | Standard HID normally uses the Windows inbox HID stack and needs no packaged driver; a nonstandard or wrongly rebound device is a different case. |
| `serial_comm` / `libserialport` | A COM port is consumed by libsigrok | It does not reveal whether the physical port is USB CDC (often inbox), FTDI, CH34x, CP210x, a Bluetooth port, or a physical UART. |
| no listed transport dependency | Frequently TCP, VISA/SCPI, external process, or local implementation | It is not evidence of a Windows device driver requirement. |

This distinction is observable in the source. The CH32H417 backend does not
use `libusb`; it dynamically uses WCH `CH375DLL64.DLL`, which depends on the
WCH `CH375_A64` kernel service. In contrast, the DSLogic backend calls
`libusb_open()` and `libusb_claim_interface()`. It may look driverless only
because DSView, Zadig, Windows, or an earlier setup already supplied an
appropriate USB binding. `libusb` itself cannot install or substitute that
binding.

Upstream `README.devices` also states that Windows USB devices may need a
driver assignment for libusb applications, and that the assignment can need to
be repeated after a firmware upload changes the USB identity. Therefore a
one-time install-all pass cannot cover every future state.

## Current known state

The package currently contains only this WCH runtime component:

```text
third_party/wch-ch375/CH375DLL64.DLL
```

It does not contain the CH375 INF, catalog, SYS file, or installer. The known
CH375 device is `USB\\VID_1A86&PID_5537`; its supported WCH binding is the
signed `CH375_A64` service. The unrelated `WCHU3Logic-libusb` package targets
`VID_1A86&PID_8025` and must not be used as its replacement.

Consequently, CH375 is the only current candidate with enough evidence to
become the first curated entry. It still needs verified redistribution
permission for the driver payload before release.

All other enabled USB/FTDI/serial source drivers require a device-by-device
audit before inclusion. The source scan can identify some VID/PID constants,
but cannot establish all of these release facts:

1. The vendor's current signed package and the exact hardware or interface IDs
   it supports.
2. Whether Windows already provides a class driver that exposes the interface
   Sigrok needs.
3. Whether installing a generic binding would remove a required vendor tool or
   COM port.
4. Whether the vendor permits redistribution of its INF, CAT, SYS, DLL, and
   installer.
5. Whether firmware loading changes the PID or presents a composite interface
   that needs separate policy.

Until that audit is complete, the global preflight may inspect and log these
devices but must not install anything for them.

## Central implementation shape

The central mechanism belongs in a Windows-only libsigrok subsystem, not in
individual hardware backends and not in the Extension. A practical location is
a new internal module near libsigrok's driver scan orchestration. It should run
lazily immediately before the first real scan, be cached for the `sr_context`,
and be skipped for operations such as `--version` and `--list-drivers`.

The module uses SetupAPI and Configuration Manager to enumerate present PnP
instances. For every manifest match it records, at minimum:

- hardware ID including VID, PID, and (where required) MI/interface number;
- PnP instance ID, service name, driver provider/version, and problem code;
- one of `ready`, `missing`, `other-binding`, `disabled`, `error`, or
  `not-present`;
- the manifest entry selected by exact matching, never by a broad USB class.

Only `missing` entries may be included in the install set. `other-binding` is
an explicit non-action state. For example, a DSLogic device already bound to a
vendor stack must not be silently rebound to WinUSB/libusbK merely because the
Sigrok backend uses libusb.

The elevated executable should be a small, separately built helper in the
Sigrok distribution. Libsigrok launches it once with `ShellExecuteExW` and
the `runas` verb. The helper receives a generated list of manifest entry IDs,
re-validates the package root and file hashes, and installs using either the
vendor's documented silent installer or `pnputil /add-driver <INF> /install`.
It must not accept arbitrary paths or INF names from the caller. A helper is
preferable to elevating `sigrok-cli.exe`, because normal device discovery then
continues as the invoking user and the privileged surface is narrowly scoped.

There is one semantic limit to the requested UX: UAC can be made one prompt
for the selected set, but Windows may still report individual package failures,
signature failures, an unplug/replug requirement, or a reboot requirement. The
preflight must re-enumerate afterward and let normal scans report only devices
that actually became usable.

## Driver manifest and package payload

The manifest is the product contract, not a generated reflection of
`configure.ac`. It should be versioned with the distribution and have a shape
similar to this:

```json
{
  "schemaVersion": 1,
  "entries": [
    {
      "id": "wch-ch375-x64",
      "match": {
        "hardwareIds": ["USB\\\\VID_1A86&PID_5537"]
      },
      "acceptableBindings": ["CH375_A64"],
      "install": {
        "architecture": "amd64",
        "method": "pnputil",
        "inf": "driver-install/wch-ch375/x64/CH375WDM.INF"
      },
      "replacementPolicy": "missing-only",
      "files": ["... hashes, sizes, and signer requirements ..."],
      "provenance": "... vendor version, source URL/date, and redistribution terms ..."
    }
  ]
}
```

Every installed file must be placed below a package-owned
`driver-install/<id>/<architecture>/` directory and included in
`package-manifest.json`. A native driver package requires its complete,
unchanged set of INF, CAT, SYS, co-installers/DLLs, and any documented helper
layout. The catalog signature, individual hashes, architecture, provenance,
and redistribution authorization must be checked during packaging and release
verification. The existing package scripts already generate full package hash
manifests and third-party notices, so this is an extension of the existing
release boundary rather than an Extension change.

Do not use the package root DLL closure as proof that a driver payload is
complete: PE dependency checks do not validate INF catalogs or Windows driver
store installation.

## Rollout plan

1. Define an explicit initial allowlist. Start with only WCH CH375 x64 after
   its signed payload and redistribution terms are confirmed. Do not call the
   mechanism "all Sigrok drivers".
2. Add the manifest, driver payload staging rules, package hashes, signature
   validation, license/provenance disclosure, and static verification.
3. Implement a Windows PnP inspection library and tests using injected PnP
   data. Integrate it centrally before the first scan, initially in
   inspect-only mode.
4. Build the restricted elevated helper. Add one-process attempt caching,
   cancellation handling, bounded waits, and post-install re-enumeration.
5. Validate on clean Windows x64 VMs with: absent device, missing driver,
   accepted UAC/install, cancelled UAC, existing correct binding, existing
   other binding, invalid signature, and unplug/replug-required outcomes.
6. Audit future candidates one at a time. Add a driver only when its exact
   match, non-replacement policy, signed package, licensing, and clean-VM test
   matrix are all available.

## Recommendation

Adopt the central preflight architecture, but define it as **install curated
drivers for currently present devices with missing bindings**. This achieves
the desired single UAC interaction without turning a broad hardware library
into an unsafe machine-wide driver installer. CH375 can be the first entry;
the remaining enabled backends should stay inspect-only until they pass the
per-device audit.
