CC ?= cc
.DEFAULT_GOAL := all
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude

# Tcl/Tk is optional for the dependency-free CLI.  On macOS, the SDK ships
# the 8.5 headers while the system frameworks provide the link targets.  A
# Linux/packaged Tcl/Tk build can override both variables, for example:
#   make TCL_CFLAGS="-I/usr/include/tcl8.6" TCL_LIBS="-ltcl8.6 -ltk8.6"
UNAME_S := $(shell uname -s 2>/dev/null)
SDKROOT ?= $(shell xcrun --show-sdk-path 2>/dev/null)
TCL_CFLAGS ?=
TCL_LIBS ?=
ifeq ($(UNAME_S),Darwin)
ifneq ($(wildcard runtime/include/tcl.h),)
  TCL_CFLAGS := -Iruntime/include -DRF_BUNDLED_TK
  TCL_LIBS := -Lruntime/lib -ltk8.6 -ltcl8.6 -Wl,-rpath,@executable_path/runtime/lib
  RF_LOCAL_TK := yes
endif
endif
ifeq ($(strip $(TCL_CFLAGS)),)
  ifeq ($(UNAME_S),Darwin)
    ifneq ($(strip $(SDKROOT)),)
      TCL_CFLAGS := -I$(SDKROOT)/System/Library/Frameworks/Tcl.framework/Versions/8.5/Headers -I$(SDKROOT)/System/Library/Frameworks/Tk.framework/Versions/8.5/Headers
    endif
  endif
endif
ifeq ($(strip $(TCL_LIBS)),)
  ifeq ($(UNAME_S),Darwin)
    TCL_LIBS := -framework Tcl -framework Tk
  endif
endif

TK_HEADERS_OK := $(shell test -f "$(SDKROOT)/System/Library/Frameworks/Tcl.framework/Versions/8.5/Headers/tcl.h" -a -f "$(SDKROOT)/System/Library/Frameworks/Tk.framework/Versions/8.5/Headers/tk.h" && echo yes)
ifeq ($(RF_LOCAL_TK),yes)
  TK_ENABLED := yes
else ifeq ($(strip $(TCL_CFLAGS)),)
  TK_ENABLED := no
else ifeq ($(UNAME_S),Darwin)
  ifeq ($(TK_HEADERS_OK),yes)
    TK_ENABLED := yes
  else
    TK_ENABLED := no
  endif
else
  TK_ENABLED := yes
endif
ifeq ($(GUI),no)
  TK_ENABLED := no
endif

TARGET := rf-workbench
SOURCES := src/main.c src/rf_model.c src/project_io.c src/geometry.c src/vacuum.c src/maxwell.c src/spatial.c src/full_modes.c src/branches.c src/plasma.c src/plot.c
ifeq ($(TK_ENABLED),yes)
  SOURCES += src/rf_tk_gui.c
  CPPFLAGS += $(TCL_CFLAGS) -DRF_HAVE_TK
  GUI_LIBS := $(TCL_LIBS)
endif
OBJECTS := $(SOURCES:.c=.o)
# Changes of runtime/compiler options must invalidate previous GUI objects.
.PHONY: FORCE
.build-config: FORCE
	@printf '%s\n' '$(CC) $(CFLAGS) $(CPPFLAGS) $(GUI_LIBS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(OBJECTS): .build-config

.PHONY: all app clean check test core-test antenna-test maxwell-test spatial-test full-test branches-test cli-test gui-test runtime print-config

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -lm $(GUI_LIBS) -o $@

src/%.o: src/%.c include/rf_model.h include/project_io.h include/geometry.h include/vacuum.h include/plasma.h include/plot.h include/maxwell.h include/spatial.h include/full_modes.h include/branches.h include/rf_quadrature.h include/rf_physics.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

src/rf_gui_script.h: src/rf_gui.tcl src/rf_gui_views.tcl tools/embed_tcl.awk
	LC_ALL=C awk -f tools/embed_tcl.awk src/rf_gui_views.tcl src/rf_gui.tcl > $@.tmp
	mv $@.tmp $@

src/rf_tk_gui.o: src/rf_gui_script.h src/rf_gui_fields.h src/rf_gui_render.h src/rf_gui_spatial.h src/rf_gui_full.h src/rf_gui_branches.h include/rf_tk_gui.h

check: $(TARGET)
	./$(TARGET) --check

CORE_OBJECTS := src/rf_model.o src/project_io.o src/geometry.o src/vacuum.o src/maxwell.o src/spatial.o src/full_modes.o src/branches.o src/plasma.o src/plot.o
rf-core-test: tests/core_test.c $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -Iinclude $^ -lm -o $@

rf-maxwell-test: tests/maxwell_test.c $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -Iinclude $^ -lm -o $@

rf-antenna-test: tests/antenna_catalogue_test.c $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -Iinclude $^ -lm -o $@

antenna-test: rf-antenna-test
	./rf-antenna-test

rf-branches-test: tests/branches_test.c $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -Iinclude $^ -lm -o $@

branches-test: rf-branches-test
	./rf-branches-test

rf-full-test: tests/full_modes_test.c $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -Iinclude $^ -lm -o $@

full-test: rf-full-test
	./rf-full-test

rf-spatial-test: tests/spatial_test.c $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -Iinclude $^ -lm -o $@

spatial-test: rf-spatial-test
	./rf-spatial-test

maxwell-test: rf-maxwell-test
	./rf-maxwell-test

core-test: rf-core-test
	./rf-core-test

cli-test: $(TARGET)
	python3 tests/cli_test.py

test: check core-test antenna-test maxwell-test spatial-test full-test branches-test cli-test

runtime:
	sh tools/build_tk_macos.sh

app: all
	sh tools/package_macos_app.sh

ifeq ($(TK_ENABLED),yes)
rf-gui-test: tests/gui_driver.c tests/gui_smoke.tcl src/rf_tk_gui.c src/rf_gui_fields.h src/rf_gui_render.h src/rf_gui_spatial.h src/rf_gui_full.h src/rf_gui_branches.h src/rf_gui_script.h $(filter-out src/main.o src/rf_tk_gui.o,$(OBJECTS))
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/gui_driver.c $(filter-out src/main.o src/rf_tk_gui.o,$(OBJECTS)) -lm $(GUI_LIBS) -o $@

gui-test: rf-gui-test
	./rf-gui-test tests/gui_smoke.tcl
endif

clean:
	rm -f src/*.o $(TARGET) rf-core-test rf-antenna-test rf-maxwell-test rf-spatial-test rf-full-test rf-branches-test rf-gui-test
	rm -f src/rf_gui_script.h src/rf_gui_script.h.tmp .build-config .build-config.tmp

print-config:
	@printf 'Tcl/Tk GUI: %s\n' '$(TK_ENABLED)'
	@printf 'TCL_CFLAGS: %s\n' '$(TCL_CFLAGS)'
	@printf 'TCL_LIBS: %s\n' '$(TCL_LIBS)'
