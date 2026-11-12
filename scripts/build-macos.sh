#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
version="${1:-1.0.0}"
if [[ $# -gt 0 ]]; then shift; fi
if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    printf '%s\n' 'La version debe tener el formato MAJOR.MINOR.PATCH.' >&2
    exit 1
fi
build_dir="${FINDNOTE_BUILD_DIR:-build}"
if ! command -v cmake >/dev/null 2>&1; then
    if [[ ! -x .tools/venv/bin/cmake ]]; then
        python3 -m venv .tools/venv
        .tools/venv/bin/python -m pip install cmake==4.2.3
    fi
    export PATH="$PWD/.tools/venv/bin:$PATH"
fi
python3 scripts/build.py --version "$version" --arch "${FINDNOTE_ARCH:-arm64}" --build-dir "$build_dir" "$@"
