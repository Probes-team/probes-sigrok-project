# WCH CH375 driver provenance

- Binaries: `CH375DLL64.DLL`, `CH375DLL.DLL`, `CH375W64.SYS`, `CH375WDM.INF`,
  `CH375WDM.CAT`, `SETUP.EXE`, `DRVSETUP64/DRVSETUP64.EXE`
- Source package: WCH CH372/CH375 Windows driver package bundled with the
  CH32H417 Logic Analyzer distribution.
- Local source path used for staging: `install/driver/CH372DRV/DRIVER/`
- Vendor/API reference: `https://www.wch-ic.com/products/CH372.html`
- SHA-256: `5ED04E4A8EB9006E92907FDC105C4D8F915408409A02C157CA492A65D9163AA7`
- Target placement: package root, next to `sigrok-cli.exe`.
- Distribution decision: this binary is intentionally shipped with the local
  Sigrok package for the CH32H417 driver; no system DLL discovery is used.

The complete x64 driver payload and hashes are recorded in
`driver-manifest.json`. The INF targets `USB\VID_1A86&PID_5537` and installs the
`CH375_A64` service using `CH375W64.SYS`. It also copies `CH375DLL.DLL` to the
WOW64 system directory, so that compatibility DLL is retained in the x64
payload. The catalog was verified as valid on 2026-08-29 with PowerShell
`Get-AuthenticodeSignature` and the Microsoft Hardware Compatibility Publisher
certificate.

The source package is local vendor software. Redistribution of the SYS, CAT,
INF and installer must be approved by the project owner before publishing a
release artifact; this checkout records the files for implementation and
verification only.

The binaries are not loaded from the local reference checkout at runtime. Keep
this record and `driver-manifest.json` synchronized whenever staged vendor
files change.
