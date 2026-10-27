#!/bin/sh
set -eu
rf_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rf_app="$rf_root/../../无限长冷等离子体工作台.app"
[ "$(uname -s)" = Darwin ] || exit 0
mkdir -p "$rf_app/Contents/MacOS" "$rf_app/Contents/Resources/examples"
cp "$rf_root/tools/Info.plist" "$rf_app/Contents/Info.plist"
# A real Mach-O app executable, rather than a shell launcher, gives macOS
# a stable application/window identity and works independently of the checkout.
cp "$rf_root/rf-workbench" "$rf_app/Contents/MacOS/CylinderRF.new"
chmod +x "$rf_app/Contents/MacOS/CylinderRF.new"
mv "$rf_app/Contents/MacOS/CylinderRF.new" "$rf_app/Contents/MacOS/CylinderRF"
if [ -d "$rf_root/runtime/lib" ]; then
    mkdir -p "$rf_app/Contents/Frameworks" "$rf_app/Contents/Resources/TclTk/lib" "$rf_app/Contents/Resources/TclTk/licenses"
    cp -f "$rf_root/runtime/lib/"*.dylib "$rf_app/Contents/Frameworks/"
    cp -R "$rf_root/runtime/lib/tcl8.6" "$rf_root/runtime/lib/tk8.6" "$rf_app/Contents/Resources/TclTk/lib/"
    cp -R "$rf_root/runtime/licenses/." "$rf_app/Contents/Resources/TclTk/licenses/"
    cp "$rf_root/runtime/BUILD.txt" "$rf_app/Contents/Resources/TclTk/"
    install_name_tool -add_rpath '@executable_path/../Frameworks' "$rf_app/Contents/MacOS/CylinderRF"
fi
rm -rf "$rf_app/Contents/Resources/examples"
mkdir -p "$rf_app/Contents/Resources/examples"
cp "$rf_root/../examples/"*.rfproj "$rf_app/Contents/Resources/examples/"
codesign --force --deep --sign - "$rf_app"
printf 'App updated: %s\n' "$rf_app"
