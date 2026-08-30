"""Generate the immutable WCH driver manifest data compiled into the helper."""

import hashlib
import json
import pathlib
import sys


def main(manifest_name: str, output_name: str) -> None:
    manifest_path = pathlib.Path(manifest_name)
    output_path = pathlib.Path(output_name)
    raw = manifest_path.read_bytes()
    manifest = json.loads(raw)
    if manifest["id"] != "wch-ch375-x64" or manifest["architecture"] != "amd64":
        raise RuntimeError("unexpected WCH driver manifest identity")

    lines = [
        "#ifndef WCH_DRIVER_MANIFEST_DATA_H",
        "#define WCH_DRIVER_MANIFEST_DATA_H",
        "#include <windows.h>",
        "struct wch_manifest_file { const wchar_t *path; DWORD size; const char *sha256; };",
        f'#define WCH_MANIFEST_SHA256 "{hashlib.sha256(raw).hexdigest()}"',
        f'#define WCH_MANIFEST_FILE_COUNT {len(manifest["files"])}',
        "static const struct wch_manifest_file wch_manifest_files[] = {",
    ]
    for entry in manifest["files"]:
        path = pathlib.PurePosixPath(entry["path"])
        if path.is_absolute() or ".." in path.parts:
            raise RuntimeError(f"unsafe WCH driver path: {path}")
        windows_path = str(path).replace("/", "\\\\")
        lines.append(
            f'    {{ L"{windows_path}", {int(entry["size"])}, "{entry["sha256"]}" }},'
        )
    lines.extend(["};", "#endif", ""])
    output_path.write_text("\n".join(lines), encoding="ascii")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
