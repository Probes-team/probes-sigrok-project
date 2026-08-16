# Third-Party Notices

This project imports source baselines from the sigrok project. The exact source
repositories, commits, archive hashes, and source license file locations are
recorded in `../upstream-manifest.json`.

For every binary release, `package.ps1` copies the imported source notices and
the MINGW64 runtime license tree into `LICENSES/`. The matching source trees are
provided in `source-disclosure/`, and the package manifest records every file
hash.

Do not remove copyright notices or license files from imported source trees.

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
