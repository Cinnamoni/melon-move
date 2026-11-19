#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
    echo "Usage: $0 BUILD_DIR OUTPUT_DIR" >&2
    exit 2
fi
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
app_dir="$2/melon-move"
if [[ ! -x "$1/entry" ]]; then
    echo "Missing backend executable at $1/entry" >&2
    exit 1
fi
if [[ -n ${QT_RCC:-} ]]; then
    rcc=$QT_RCC
elif command -v rcc6 >/dev/null; then
    rcc=rcc6
elif [[ -x /usr/lib/qt6/libexec/rcc ]]; then
    rcc=/usr/lib/qt6/libexec/rcc
else
    rcc=rcc
fi

mkdir -p -- "$app_dir/backend"
"$rcc" --binary --format-version 2 --no-compress -o "$app_dir/resources.rcc" "$source_dir/application.qrc"
install -m 0644 -- "$source_dir/manifest.json" "$source_dir/icon.png" "$source_dir/LICENSE" "$app_dir"
install -m 0755 -- "$1/entry" "$app_dir/backend/entry"
printf 'AppLoad package written to %s\n' "$app_dir"
