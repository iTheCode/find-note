#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $# -ne 1 ]]; then
    printf '%s\n' 'Uso: bash scripts/prepare-release.sh MAJOR.MINOR.PATCH' >&2
    exit 1
fi
# Local macOS packaging only. GitHub publishes the three packages built by CI.
bash scripts/build-macos.sh "$1" --package
