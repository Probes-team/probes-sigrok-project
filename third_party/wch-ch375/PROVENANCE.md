# WCH CH375 runtime provenance

- Binary: `CH375DLL64.DLL`
- Source package: WCH CH372/CH375 Windows driver package bundled with the
  CH32H417 Logic Analyzer distribution.
- Local source path used for staging: `install/driver/CH372DRV/DRIVER/CH375DLL64.dll`
- Vendor/API reference: `https://www.wch-ic.com/products/CH372.html`
- SHA-256: `5ED04E4A8EB9006E92907FDC105C4D8F915408409A02C157CA492A65D9163AA7`
- Target placement: package root, next to `sigrok-cli.exe`.
- Distribution decision: this binary is intentionally shipped with the local
  Sigrok package for the CH32H417 driver; no system DLL discovery is used.

The binary is not loaded from the local reference checkout at runtime. Keep
this record synchronized whenever the staged vendor DLL changes.
