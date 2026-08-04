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
    (cd "$component_build" && make DESTDIR="$stage_root" install)
}

build_component libsigrok --enable-all-drivers=check --disable-bindings
export PKG_CONFIG_PATH="$stage_root$prefix/lib/pkgconfig"
build_component libsigrokdecode
build_component sigrok-cli

runtime_supported="$work_root/runtime-supported.txt"
(PATH="$stage_root$prefix/bin:/mingw64/bin:/usr/bin" "$stage_root$prefix/bin/sigrok-cli.exe" --list-supported || true) >"$runtime_supported" 2>&1

python3.14 - "$work_root/build-manifest.json" "$project_commit" "$target" "$configuration" "$stage_root" "$source_root/libsigrok/configure.ac" "$build_root/libsigrok/config.log" "$runtime_supported" <<'PY'
import json
import pathlib
import re
import sys

output, project_commit, target, configuration, stage, configure_ac, config_log, runtime_supported = sys.argv[1:]
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
    'stagePrefix': '/probes-sigrok',
}
pathlib.Path(output).write_text(json.dumps(payload, indent=2) + '\n')
PY
