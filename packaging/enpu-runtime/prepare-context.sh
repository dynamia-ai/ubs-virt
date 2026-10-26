#!/usr/bin/env bash
set -euo pipefail
export GIT_NO_REPLACE_OBJECTS=1
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
output=${1:-"$root/.build/enpu-runtime"}
commit=46b6d55a892429008b1e02f96b0a0f81d1176db8
# The source release is independent of the packaging revision.
git -C "$root" cat-file -e "$commit^{commit}"
mkdir -p "$output"
git -C "$root" archive --format=tar "$commit" ubs-virt-enpu/vcann-rt > "$output/source.tar"
git -C "$root" show "$commit:LICENSE" > "$output/LICENSE.ubs-virt"
cp "$root/packaging/enpu-runtime/"{Dockerfile,build-runtime.sh} "$output/"
python3 - "$output" "$commit" <<'PY'
import hashlib, json, pathlib, sys
out = pathlib.Path(sys.argv[1])
metadata = {"repository": "https://github.com/dynamia-ai/ubs-virt", "tag": "1.0.0", "commit": sys.argv[2],
            "archive_sha256": hashlib.sha256((out / "source.tar").read_bytes()).hexdigest()}
(out / "source.json").write_text(json.dumps(metadata, indent=2) + "\n")
PY
printf 'Build context: %s\n' "$output"
