# C11 + Tcl/Tk workbench is the default application.
.DEFAULT_GOAL := all
# Delegated goals share generated headers and build configuration. Keep root
# recipes sequential; the workbench sub-make still compiles with the requested -j.
.NOTPARALLEL:
.PHONY: all build app run gui check vacuum plasma spatial branches geometry clean portable pdf

all build:
	$(MAKE) -C 工程文件

app: all
	$(MAKE) -C 工程文件 app

run gui: all
	./工程文件/.build/rf-workbench --gui

check: all
	./工程文件/.build/rf-workbench --check

vacuum: all
	./工程文件/.build/rf-workbench --vacuum $(ARGS)

plasma: all
	./工程文件/.build/rf-workbench --plasma $(ARGS)

branches: all
	./工程文件/.build/rf-workbench --branches $(ARGS)

spatial: all
	./工程文件/.build/rf-workbench --spatial $(ARGS)

geometry: all
	./工程文件/.build/rf-workbench --geometry $(ARGS)

portable:
	$(MAKE) -C 工程文件

# 主LaTeX已内嵌图表与数据；交付版仅编译它，不依赖已移除的测试工具。
pdf:
	mkdir -p tmp/pdfs/build
	cd 天线电流谱文档 && latexmk -xelatex -interaction=nonstopmode -halt-on-error -outdir=../tmp/pdfs/build Finite_width_antenna_current_spectra.tex
	cp tmp/pdfs/build/Finite_width_antenna_current_spectra.pdf 天线电流谱文档/Finite_width_antenna_current_spectra.pdf

clean:
	$(MAKE) -C 工程文件 clean
	rm -rf tmp
	rm -f .DS_Store 工程文件/.DS_Store
