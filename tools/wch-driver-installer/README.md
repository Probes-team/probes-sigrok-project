# WCH Driver Installer Helper

`wch-driver-installer.exe` is the only process elevated by the CH375 Sigrok
driver. It accepts only the compiled-in `wch-ch375-x64` entry ID. At build time
the driver manifest is converted to fixed file paths, sizes and SHA-256 values;
the helper checks these and the full manifest digest before it starts the
vendor's fixed `SETUP.EXE /S` command.

The helper does not accept an INF path, working directory, executable name or
additional installer arguments. It returns nonzero on validation, launch,
timeout, or vendor-installer failure.
