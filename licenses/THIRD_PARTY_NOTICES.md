# Third-Party Notices

This project imports source baselines from the sigrok project. The exact source
repositories, commits, archive hashes, and source license file locations are
recorded in `../upstream-manifest.json`.

For every binary release, `package.ps1` copies the imported source notices and
the MINGW64 runtime license tree into `LICENSES/`. The matching source trees are
provided in `source-disclosure/`, and the package manifest records every file
hash.

Do not remove copyright notices or license files from imported source trees.
