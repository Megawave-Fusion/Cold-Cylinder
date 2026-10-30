#!/bin/sh
# Expand the native runtime once per archive digest, outside the checkout.
set -eu
rf_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
[ "$(uname -s)" = Darwin ] || { printf '%s\n' 'The bundled Aqua runtime requires macOS.' >&2; exit 1; }
case "$(uname -m)" in
    arm64) rf_arch=arm64 ;;
    x86_64) rf_arch=x64 ;;
    *) printf '%s\n' 'Unsupported macOS CPU architecture.' >&2; exit 1 ;;
esac
rf_archive="$rf_root/deps/macos-$rf_arch.zip"
[ -f "$rf_archive" ] || { printf 'Missing runtime archive: %s\n' "$rf_archive" >&2; exit 1; }
rf_hash=$(shasum -a 256 "$rf_archive" | cut -d ' ' -f 1)
rf_cache="${COLD_CYLINDER_CACHE_DIR:-$HOME/Library/Caches/Cold-Cylinder}/runtime"
case "$rf_cache" in
    /*) ;;
    *) printf '%s\n' 'COLD_CYLINDER_CACHE_DIR must be an absolute path.' >&2; exit 1 ;;
esac
rf_destination="$rf_cache/macos-$rf_arch-$(printf '%s' "$rf_hash" | cut -c 1-12)"
case "${1:---prepare}" in
    --path) printf '%s\n' "$rf_destination"; exit 0 ;;
    --prepare) ;;
    *) printf '%s\n' 'Usage: prepare_runtime.sh [--path|--prepare]' >&2; exit 1 ;;
esac
rf_valid() {
    [ -f "$1/.cold-cylinder-complete" ] &&
    [ "$(cat "$1/.cold-cylinder-complete")" = "$rf_hash" ] &&
    [ -f "$1/include/tcl.h" ] && [ -f "$1/include/tk.h" ] &&
    [ -f "$1/lib/libtcl8.6.dylib" ] && [ -f "$1/lib/libtk8.6.dylib" ] &&
    [ -f "$1/lib/tcl8.6/init.tcl" ] && [ -f "$1/lib/tk8.6/tk.tcl" ] &&
    [ -d "$1/licenses" ] && [ -f "$1/BUILD.txt" ]
}
if rf_valid "$rf_destination"; then exit 0; fi
[ ! -e "$rf_destination" ] || { printf 'Incomplete dependency cache: %s\n' "$rf_destination" >&2; exit 1; }
mkdir -p "$rf_cache"
rf_stage=$(mktemp -d "$rf_cache/.extract-$rf_arch.XXXXXX")
trap 'rm -rf "$rf_stage"' EXIT
trap 'exit 1' HUP INT TERM
ditto -x -k "$rf_archive" "$rf_stage"
printf '%s\n' "$rf_hash" > "$rf_stage/.cold-cylinder-complete"
rf_valid "$rf_stage" || { printf '%s\n' 'The runtime archive is incomplete.' >&2; exit 1; }
# mkdir is the publication lock. Another checkout may be preparing the same
# digest; reuse its completed result, never nest or overwrite cache directories.
rf_lock="$rf_destination.lock"
if mkdir "$rf_lock" 2>/dev/null; then
    trap 'rmdir "$rf_lock"; rm -rf "$rf_stage"' EXIT
    if [ ! -e "$rf_destination" ]; then mv "$rf_stage" "$rf_destination"; fi
else
    printf 'Runtime cache is being prepared elsewhere: %s\n' "$rf_destination" >&2
    exit 1
fi
rf_valid "$rf_destination" || { printf '%s\n' 'Failed to publish the runtime cache.' >&2; exit 1; }
