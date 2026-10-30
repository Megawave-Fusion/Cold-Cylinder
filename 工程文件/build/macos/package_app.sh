#!/bin/sh
set -eu
# 从空暂存目录组装，验证后再替换旧应用；避免历史资源残留或失败破坏可用版本。
rf_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
rf_workspace=$(CDPATH= cd -- "$rf_root/.." && pwd)
[ "$(uname -s)" = Darwin ] || { printf '%s\n' 'macOS packaging requires macOS; use Build-Cold-Cylinder-Windows.cmd on Windows.' >&2; exit 1; }
[ -x "$rf_root/.build/rf-workbench" ] || { printf '%s\n' 'Build the GUI executable before packaging.' >&2; exit 1; }
rf_stage=$(mktemp -d "$rf_workspace/.cold-cylinder-macos.XXXXXX")
rf_app="$rf_stage/Cold-Cylinder.app"
rf_target="$rf_workspace/Cold-Cylinder.app"
rf_backup="$rf_stage/previous.app"
rf_cleanup() {
    # 直接检查磁盘状态，信号即使落在 mv 与下一条赋值之间也能恢复旧应用。
    if [ -e "$rf_backup" ] && [ ! -e "$rf_target" ]; then
        mv "$rf_backup" "$rf_target"
    fi
    rm -rf "$rf_stage"
}
trap rf_cleanup EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$rf_app/Contents/MacOS" "$rf_app/Contents/Resources/examples"
cp "$rf_root/build/macos/Info.plist" "$rf_app/Contents/Info.plist"
# A real Mach-O app executable, rather than a shell launcher, gives macOS
# a stable application/window identity and works independently of the checkout.
cp "$rf_root/.build/rf-workbench" "$rf_app/Contents/MacOS/CylinderRF"
chmod +x "$rf_app/Contents/MacOS/CylinderRF"
if [ -d "$rf_root/.build/runtime/lib" ]; then
    # 共享库放Frameworks，Tcl脚本资源放Resources；桥接初始化代码使用同一约定。
    mkdir -p "$rf_app/Contents/Frameworks" "$rf_app/Contents/Resources/TclTk/lib" "$rf_app/Contents/Resources/TclTk/licenses"
    cp -f "$rf_root/.build/runtime/lib/"*.dylib "$rf_app/Contents/Frameworks/"
    cp -R "$rf_root/.build/runtime/lib/tcl8.6" "$rf_root/.build/runtime/lib/tk8.6" "$rf_app/Contents/Resources/TclTk/lib/"
    cp -R "$rf_root/.build/runtime/licenses/." "$rf_app/Contents/Resources/TclTk/licenses/"
    cp "$rf_root/.build/runtime/BUILD.txt" "$rf_app/Contents/Resources/TclTk/"
    install_name_tool -add_rpath '@executable_path/../Frameworks' "$rf_app/Contents/MacOS/CylinderRF"
fi
cp "$rf_root/examples/"*.rfproj "$rf_app/Contents/Resources/examples/"
codesign --force --deep --sign - "$rf_app"
codesign --verify --deep --strict "$rf_app"
"$rf_app/Contents/MacOS/CylinderRF" --check

# 对外ZIP同时附总说明和既有PDF；编译与打包不重新生成理论文档。
rf_arch=$(lipo -archs "$rf_app/Contents/MacOS/CylinderRF")
case "$rf_arch" in
    arm64) rf_arch_tag=arm64 ;;
    x86_64) rf_arch_tag=x64 ;;
    *) rf_arch_tag=universal ;;
esac
rf_bundle="$rf_stage/Cold-Cylinder-macOS-$rf_arch_tag"
mkdir -p "$rf_bundle/天线电流谱文档" "$rf_workspace/交付"
cp "$rf_workspace/README.md" "$rf_bundle/README.md"
cp "$rf_workspace/天线电流谱文档/Finite_width_antenna_current_spectra.pdf" "$rf_bundle/天线电流谱文档/"
ditto "$rf_app" "$rf_bundle/Cold-Cylinder.app"
rf_zip="$rf_stage/Cold-Cylinder-macOS-$rf_arch_tag.zip"
ditto -c -k --sequesterRsrc --keepParent "$rf_bundle" "$rf_zip"
if [ -e "$rf_target" ]; then
    mv "$rf_target" "$rf_backup"
fi
mv "$rf_app" "$rf_target"
mv -f "$rf_zip" "$rf_workspace/交付/Cold-Cylinder-macOS-$rf_arch_tag.zip"
printf 'App updated: %s\nmacOS ZIP: %s\n' "$rf_target" "$rf_workspace/交付/Cold-Cylinder-macOS-$rf_arch_tag.zip"
