#!/usr/bin/env bash
set -euo pipefail

project_root="$1"
configuration="$2"
target="$3"
project_commit="$4"
work_root="$project_root/.work"
source_root="$work_root/autotools-src"
build_root="$work_root/build"
stage_root="$work_root/stage"
prefix="/probes-sigrok"

rm -rf "$source_root" "$build_root" "$stage_root"
mkdir -p "$source_root" "$build_root" "$stage_root"
cp -a "$project_root/src/." "$source_root/"

jobs="${NUMBER_OF_PROCESSORS:-2}"
common_configure=("--host=$target" "--prefix=$prefix")
if [ "$configuration" = "Debug" ]; then
    common_configure+=("CFLAGS=-O0 -g")
else
    common_configure+=("CFLAGS=-O2")
fi

build_component() {
    local component="$1"
    shift
    local component_source="$source_root/$component"
    local component_build="$build_root/$component"

    (cd "$component_source" && ./autogen.sh)
    mkdir -p "$component_build"
    (cd "$component_build" && "$component_source/configure" "${common_configure[@]}" "$@")
    (cd "$component_build" && make -j"$jobs")
    case "$component" in
        libsigrok|libsigrokdecode)
            check_log="$component_build/check.log"
            if ! (cd "$component_build" && make check >"$check_log" 2>&1); then
                cat "$check_log"
                exit 1
            fi
            cat "$check_log"
            check_total=$(sed -n 's/^# TOTAL: *//p' "$check_log" | tail -1)
            if [ -z "$check_total" ] || [ "$check_total" -eq 0 ]; then
                echo "No effective tests were registered for $component." >&2
                exit 1
            fi
            ;;
    esac
    (cd "$component_build" && make DESTDIR="$stage_root" install)
}

build_component libsigrok --enable-all-drivers=check --disable-bindings
export PKG_CONFIG_PATH="$stage_root$prefix/lib/pkgconfig"
build_component libsigrokdecode
build_component sigrok-cli

helper_root="$work_root/wch-driver-installer"
helper_header="$helper_root/driver_manifest_data.h"
helper_exe="$helper_root/wch-driver-installer.exe"
mkdir -p "$helper_root"
python3.14 "$project_root/tools/wch-driver-installer/generate_manifest_header.py" \
    "$project_root/third_party/wch-ch375/driver-manifest.json" "$helper_header"
x86_64-w64-mingw32-gcc -O2 -municode -Wall -Wextra -Werror \
    -I"$helper_root" "$project_root/tools/wch-driver-installer/wch-driver-installer.c" \
    -ladvapi32 -o "$helper_exe"

runtime_supported="$work_root/runtime-supported.txt"
(PATH="$stage_root$prefix/bin:/mingw64/bin:/usr/bin" "$stage_root$prefix/bin/sigrok-cli.exe" --list-supported || true) >"$runtime_supported" 2>&1

python3.14 - "$work_root/build-manifest.json" "$project_commit" "$target" "$configuration" "$stage_root" "$source_root/libsigrok/configure.ac" "$build_root/libsigrok/config.log" "$runtime_supported" "$source_root/sigrok-firmware" "$build_root/libsigrok/check.log" "$build_root/libsigrokdecode/check.log" <<'PY'
import json
import pathlib
import re
import sys

output, project_commit, target, configuration, stage, configure_ac, config_log, runtime_supported, firmware_root, libsigrok_check, libsigrokdecode_check = sys.argv[1:]
stage_root = pathlib.Path(stage)
drivers = []
configure_text = pathlib.Path(configure_ac).read_text(encoding='utf-8')
config_log_text = pathlib.Path(config_log).read_bytes().decode('utf-8', errors='replace')
for match in re.finditer(r'SR_DRIVER\(\[.*?\], \[([^]]+)\](?:, \[([^]]*)\])?\)', configure_text):
    driver_id, dependencies = match.groups()
    condition = 'HW_' + re.sub(r'[^A-Z0-9]', '_', driver_id.upper()) + '_TRUE'
    state = re.search(r"^" + re.escape(condition) + r"='([^']*)'$", config_log_text, re.MULTILINE)
    if not state:
        raise RuntimeError(f'missing configure state for driver {driver_id}')
    drivers.append({
        'id': driver_id,
        'enabled': state.group(1) == '',
        'dependencies': [] if not dependencies else dependencies.split(),
    })

for line in (stage_root / 'probes-sigrok' / 'lib' / 'pkgconfig' / 'libsigrok.pc').read_text().splitlines():
    if line.startswith('Version:'):
        libsigrok_version = line.split(':', 1)[1].strip()
        break
else:
    libsigrok_version = 'unknown'
decoders = sorted(p.name for p in (stage_root / 'probes-sigrok' / 'share' / 'libsigrokdecode' / 'decoders').iterdir() if p.is_dir())
firmware_root = pathlib.Path(firmware_root)
firmware = sorted(str(p.relative_to(firmware_root)).replace('\\', '/') for p in firmware_root.rglob('*') if p.is_file())

def check_summary(component, path):
    text = pathlib.Path(path).read_text(encoding='utf-8', errors='replace')
    values = {}
    for name in ('TOTAL', 'PASS', 'SKIP', 'XFAIL', 'FAIL', 'XPASS', 'ERROR'):
        match = re.search(r'^# ' + name + r':\s*(\d+)\s*$', text, re.MULTILINE)
        if not match:
            raise RuntimeError(f'missing {name} summary for {component}')
        values[name.lower()] = int(match.group(1))
    if not values['total'] or values['fail'] or values['error']:
        raise RuntimeError(f'unsuccessful test summary for {component}: {values}')
    values['component'] = component
    return values

runtime_drivers = []
in_driver_section = False
for line in pathlib.Path(runtime_supported).read_text(encoding='utf-8', errors='replace').splitlines():
    if line == 'Supported hardware drivers:':
        in_driver_section = True
        continue
    if in_driver_section and not line.strip():
        break
    if in_driver_section:
        match = re.match(r'^  (\S+)\s+', line)
        if match:
            runtime_drivers.append(match.group(1))
payload = {
    'configuration': configuration,
    'target': target,
    'projectCommit': project_commit,
    'libsigrokVersion': libsigrok_version,
    'configureArguments': ['--enable-all-drivers=check', '--disable-bindings'],
    'drivers': drivers,
    'runtimeDrivers': sorted(set(runtime_drivers)),
    'decoders': decoders,
    'firmware': firmware,
    'testSuites': [
        check_summary('libsigrok', libsigrok_check),
        check_summary('libsigrokdecode', libsigrokdecode_check),
    ],
    'stagePrefix': '/probes-sigrok',
}
pathlib.Path(output).write_text(json.dumps(payload, indent=2) + '\n')
PY
