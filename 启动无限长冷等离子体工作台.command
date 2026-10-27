#!/bin/sh
# Native C11 + Tcl/Tk launcher.
set -eu
rf_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rf_binary="$rf_root/工程文件/c_workbench/rf-workbench"
if [ ! -x "$rf_binary" ]; then
	make -C "$rf_root/工程文件/c_workbench"
fi
exec "$rf_binary" --gui "$@"
