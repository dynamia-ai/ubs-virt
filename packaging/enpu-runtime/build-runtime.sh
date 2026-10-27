#!/usr/bin/env bash
set -euo pipefail

cann_version=$1
cann_env="/usr/local/Ascend/cann-$cann_version/set_env.sh"
[[ -f "$cann_env" ]] || { printf 'CANN environment not found: %s\n' "$cann_env" >&2; exit 1; }
set +u
source "$cann_env"
set -u
export ENPU_ASCEND_DRIVER_PATH=/usr/local/Ascend
[[ -n "${ASCEND_HOME_PATH:-}" ]] || { echo 'CANN did not set ASCEND_HOME_PATH' >&2; exit 1; }
for file in /usr/local/Ascend/driver/include/dcmi_interface_api.h /usr/local/Ascend/driver/lib64/driver/libdcmi.so; do
    [[ -s "$file" ]] || { printf 'Driver SDK file not found: %s\n' "$file" >&2; exit 1; }
done

mkdir -p /tmp/enpu-source /output
tar -xf /input/source.tar -C /tmp/enpu-source
cd /tmp/enpu-source/ubs-virt-enpu/vcann-rt
bash make_build.sh
install -m 0644 build/libvruntime.so /output/libvruntime.so
install -m 0755 build/enpu-monitor /output/enpu-monitor
printf '%s\n' /usr/local/enpu/vcann-rt/lib/libvruntime.so > /output/ld.so.preload
chmod 0644 /output/ld.so.preload

cp /input/LICENSE.ubs-virt /output/LICENSE.ubs-virt
python3 - "$cann_version" "$2" "$3" <<'PYTHON'
import hashlib, json, pathlib, struct, sys
out = pathlib.Path('/output')
source = json.loads(pathlib.Path('/input/source.json').read_text())
assert source['commit'] == sys.argv[3], 'Source revision does not match image metadata'
assert hashlib.sha256(pathlib.Path('/input/source.tar').read_bytes()).hexdigest() == source['archive_sha256']
for name in ('libvruntime.so', 'enpu-monitor'):
    header = (out / name).read_bytes()[:20]
    if header[:6] != b'\x7fELF\x02\x01' or struct.unpack('<H', header[18:20])[0] != 183:
        raise SystemExit('Expected AArch64 ELF: ' + name)
assets = {name: hashlib.sha256((out / name).read_bytes()).hexdigest()
          for name in ('libvruntime.so', 'enpu-monitor', 'ld.so.preload', 'LICENSE.ubs-virt')}
info = {'source': source, 'builder': {'image': sys.argv[2], 'architecture': 'arm64', 'os': 'linux'},
        'cann_version': sys.argv[1], 'driver_sdk': {'source': 'builder-image'}, 'assets': assets}
(out / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n')
assets['build-info.json'] = hashlib.sha256((out / 'build-info.json').read_bytes()).hexdigest()
(out / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in assets.items()))
PYTHON
cd /output
sha256sum -c SHA256SUMS
