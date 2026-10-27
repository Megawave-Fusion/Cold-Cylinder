# C11 + Tcl/Tk workbench is the default application.
.DEFAULT_GOAL := all
# Delegated goals share generated headers and build configuration. Keep root
# recipes sequential; the workbench sub-make still compiles with the requested -j.
.NOTPARALLEL:
.PHONY: all build app run gui check vacuum plasma spatial branches geometry test gui-test clean portable pdf

all build:
	$(MAKE) -C 工程文件/c_workbench

app: all
	$(MAKE) -C 工程文件/c_workbench app

gui-test:
	$(MAKE) -C 工程文件/c_workbench gui-test

run gui: all
	./工程文件/c_workbench/rf-workbench --gui

check: all
	./工程文件/c_workbench/rf-workbench --check

vacuum: all
	./工程文件/c_workbench/rf-workbench --vacuum $(ARGS)

plasma: all
	./工程文件/c_workbench/rf-workbench --plasma $(ARGS)

branches: all
	./工程文件/c_workbench/rf-workbench --branches $(ARGS)

spatial: all
	./工程文件/c_workbench/rf-workbench --spatial $(ARGS)

geometry: all
	./工程文件/c_workbench/rf-workbench --geometry $(ARGS)

test:
	$(MAKE) -C 工程文件/c_workbench test

portable:
	$(MAKE) -C 工程文件/c_workbench

pdf:
	$(MAKE) -C 工程文件/c_workbench rf-antenna-test
	mkdir -p tmp/pdfs/data tmp/pdfs/build
	./工程文件/c_workbench/rf-antenna-test tmp/pdfs/data/antenna_checks.csv tmp/pdfs/data/double_saddle_geometry.csv tmp/pdfs/data/prescribed_current_geometry.csv tmp/pdfs/data/prescribed_current_spectra.csv
	python3 工程文件/c_workbench/tools/generate_antenna_pdf_figures.py
	cd 天线电流谱文档 && latexmk -xelatex -interaction=nonstopmode -halt-on-error -outdir=../tmp/pdfs/build Finite_width_antenna_current_spectra.tex
	cp tmp/pdfs/build/Finite_width_antenna_current_spectra.pdf 天线电流谱文档/Finite_width_antenna_current_spectra.pdf

clean:
	$(MAKE) -C 工程文件/c_workbench clean
