# Third-Party Notices

This project imports source baselines from the sigrok project. The exact source
repositories, commits, archive hashes, and source license file locations are
recorded in `../upstream-manifest.json`.

For every binary release, `package.ps1` copies the imported source notices and
the MINGW64 runtime license tree into `LICENSES/`. The matching source trees are
provided in `source-disclosure/`, and the package manifest records every file
hash.

Do not remove copyright notices or license files from imported source trees.

## DSView decoder imports (local migration batch)

The following decoder sources are copied from the local DSView fork at
`../dsview_logic_runtime_local/src/DSView/libsigrokdecode4DSL/decoders/`.
The original GPLv2-or-later headers remain in each imported source file.

- `a7105/`: Copyright (C) 2014 Jens Steinhauser; GPLv2-or-later; imported 2026-08-17.
- `spi_tpm/`: Copyright (C) 2021 Jordan Ovrè (ghecko78); GPLv2-or-later; imported 2026-08-17.
- `c2/`: Copyright (C) 2024 DreamSourceLab; GPLv2-or-later; imported 2026-08-18.
- `can-fd/`: Copyright (C) 2012-2013 Uwe Hermann, 2019 Stephan Thiele, and 2024 DreamSourceLab; GPLv2-or-later; imported 2026-08-18.
- `cjtag-oscan0/`: Copyright (C) 2012-2015 Uwe Hermann, 2019 DreamSourceLab, Zhiyuan Wan, and Kongou Hikari; GPLv2-or-later; imported 2026-08-18.
- `mipi_dsi/`: Copyright (C) 2010-2016 Uwe Hermann and 2019 DreamSourceLab; GPLv2-or-later; imported 2026-08-18.
- `mipi_rffe/`: Copyright (C) 2010-2016 Uwe Hermann and 2023 DreamSourceLab; GPLv2-or-later; imported 2026-08-18.
- `smart_qspi/`: Copyright (C) 2011 Gareth McMullin, 2012-2014 Uwe Hermann, and 2024 DreamSourceLab; GPLv2-or-later; imported 2026-08-18.
- `ufcs/`: Copyright (C) 2023 Edison Ren; GPLv2-or-later; imported 2026-08-18.

Only synthetic protocol-data fixtures are used for validation. No customer captures,
real VMKs, or other proprietary samples are included.

DreamSourceLab DSLogic FPGA bitstreams and FX2 firmware images are sourced
from DreamSourceLab/DSView under the MIT license at
`src/sigrok-firmware/dreamsourcelab-dslogic/licenses/license.txt`. The FPGA
bitstreams plus `DSLogic.fw` and `DSLogicPro.fw` come from v1.3.2
(`03131dd261f08f84262c0172fd051b16cc3bb586`); `DSLogicBasic.fw` and
`DSLogicPlus.fw` come from the earlier public revision
`f17c612cb1d600a50ca4cdf804967770364b4f08`. Their file-level provenance and
checksums are recorded in `config/dreamsourcelab-dslogic-firmware.json`.

The WCH CH32H417 driver dynamically uses the vendor-supplied
`CH375DLL64.DLL`. The exact binary is shipped at the package root next to
`sigrok-cli.exe`; its source record, checksum, and distribution notice are
provided in `source-disclosure/wch-ch375/PROVENANCE.md` and
`source-disclosure/wch-ch375/LICENSE.txt`. The local staging record is kept in
`third_party/wch-ch375/`.
