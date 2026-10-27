#!/bin/sh
# Build a local Aqua runtime; no system frameworks or other apps are modified.
set -eu
rf_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rf_build=$(mktemp -d /tmp/rf-tk.XXXXXX)
rf_version=8.6.18
trap 'printf "Build sources and logs: %s\n" "$rf_build"' EXIT
[ "$(uname -s)" = Darwin ] || { echo 'This runtime builder requires macOS.' >&2; exit 1; }
for rf_part in tcl tk; do
    curl -fL --retry 2 "https://github.com/tcltk/$rf_part/archive/refs/tags/core-8-6-18.tar.gz" -o "$rf_build/$rf_part.tar.gz"
done
(cd "$rf_build" && printf '%s\n' \
 '8b9b554f3539b37bea20ee6cef14faa63df1259896e466328f596eca9f0c49a5  tcl.tar.gz' \
 '8c1d775126c39124c89752737c32c23636b3cd308d0445977aec58cfbc42c46c  tk.tar.gz' | shasum -a 256 -c -)
for rf_part in tcl tk; do tar -xzf "$rf_build/$rf_part.tar.gz" -C "$rf_build"; done
(cd "$rf_build/tcl-core-8-6-18/unix" && ./configure --prefix="$rf_build/install" --enable-threads --enable-shared && make -j4 && make install) > "$rf_build/tcl.log" 2>&1
(cd "$rf_build/tk-core-8-6-18/unix" && ./configure --prefix="$rf_build/install" --with-tcl="$rf_build/install/lib" --enable-aqua --enable-threads --enable-shared && make -j4 && make install) > "$rf_build/tk.log" 2>&1
mkdir -p "$rf_root/runtime/lib" "$rf_root/runtime/include" "$rf_root/runtime/licenses"
cp -R "$rf_build/install/include/." "$rf_root/runtime/include/"
cp -R "$rf_build/install/lib/tcl8.6" "$rf_build/install/lib/tk8.6" "$rf_root/runtime/lib/"
cp -f "$rf_build/install/lib/libtcl8.6.dylib" "$rf_build/install/lib/libtk8.6.dylib" "$rf_root/runtime/lib/"
for rf_part in tcl tk; do
    cp "$rf_build/$rf_part-core-8-6-18/license.terms" "$rf_root/runtime/licenses/$rf_part.txt"
    install_name_tool -id "@rpath/lib${rf_part}8.6.dylib" "$rf_root/runtime/lib/lib${rf_part}8.6.dylib"
done
install_name_tool -change "$rf_build/install/lib/libtcl8.6.dylib" '@loader_path/libtcl8.6.dylib' "$rf_root/runtime/lib/libtk8.6.dylib"
codesign --force --sign - "$rf_root/runtime/lib/libtcl8.6.dylib" "$rf_root/runtime/lib/libtk8.6.dylib"
printf 'Tcl/Tk %s (%s), built from pinned tcltk/tcl and tcltk/tk core-8-6-18.\n' "$rf_version" "$(uname -m)" > "$rf_root/runtime/BUILD.txt"
printf 'Runtime ready. Run make to link the workbench against it.\n'
