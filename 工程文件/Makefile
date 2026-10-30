CC ?= cc
.DEFAULT_GOAL := all
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
CPPFLAGS += -I.build -Isrc -Iui

# The dependency archive stays in the source tree; expanded files live in the
# user's cache. Only the symlink used by the executable is created under .build.
UNAME_S := $(shell uname -s 2>/dev/null)
TCL_CFLAGS ?=
TCL_LIBS ?=
TK_ENABLED := no
ifeq ($(UNAME_S),Darwin)
ifneq ($(wildcard deps/macos-$(shell uname -m | sed s/x86_64/x64/).zip),)
ifeq ($(strip $(TCL_CFLAGS)$(TCL_LIBS)),)
  RF_RUNTIME := $(shell sh build/macos/prepare_runtime.sh --path)
  TCL_CFLAGS := -I"$(RF_RUNTIME)/include" -DRF_BUNDLED_TK
  TCL_LIBS := -L"$(RF_RUNTIME)/lib" -ltk8.6 -ltcl8.6 -Wl,-rpath,@executable_path/runtime/lib
  RF_LOCAL_TK := yes
endif
endif
endif
ifneq ($(strip $(TCL_CFLAGS)$(TCL_LIBS)),)
  TK_ENABLED := yes
endif
ifeq ($(GUI),no)
  TK_ENABLED := no
endif

TARGET := .build/rf-workbench
SOURCES := $(sort $(wildcard src/*.c))
ifeq ($(TK_ENABLED),yes)
  SOURCES += ui/rf_tk_gui.c
  CPPFLAGS += $(TCL_CFLAGS) -DRF_HAVE_TK
  GUI_LIBS := $(TCL_LIBS)
  ifeq ($(RF_LOCAL_TK),yes)
    RUNTIME_READY := .build/runtime
  endif
endif
OBJECTS := $(addprefix .build/,$(notdir $(SOURCES:.c=.o)))
HEADERS := $(wildcard src/*.h ui/*.h)

.PHONY: all app check runtime clean print-config FORCE
all: $(TARGET)

.build:
	mkdir -p "$@"

# Record all compiler/linker choices so switching GUI or toolchains rebuilds
# the objects. A content comparison leaves timestamps untouched on normal runs.
.build/config: FORCE | .build
	@printf '%s\n' '$(CC) $(CFLAGS) $(CPPFLAGS) $(GUI_LIBS)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" || mv "$@.tmp" "$@"
	@rm -f "$@.tmp"

.build/runtime: build/macos/prepare_runtime.sh $(wildcard deps/macos-*.zip) .build/config | .build
	sh build/macos/prepare_runtime.sh --prepare
	ln -sfn "$(RF_RUNTIME)" "$@"

$(TARGET): $(OBJECTS) .build/config | $(RUNTIME_READY)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(OBJECTS) -lm $(GUI_LIBS) -o "$@"

.build/%.o: src/%.c $(HEADERS) .build/config | .build $(RUNTIME_READY)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c "$<" -o "$@"

.build/%.o: ui/%.c $(HEADERS) .build/config | .build $(RUNTIME_READY)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c "$<" -o "$@"

.build/rf_gui_script.h: ui/rf_gui.tcl ui/rf_gui_views.tcl build/embed_tcl.awk | .build
	LC_ALL=C awk -f build/embed_tcl.awk ui/rf_gui_views.tcl ui/rf_gui.tcl > "$@.tmp"
	mv "$@.tmp" "$@"

.build/rf_tk_gui.o: .build/rf_gui_script.h

check: $(TARGET)
	./$(TARGET) --check

# Rebuild the pinned dependency archive, rather than expanding it in the repo.
runtime:
	sh build/macos/build_runtime.sh

ifeq ($(TK_ENABLED),yes)
app: all
	sh build/macos/package_app.sh
else
app:
	@printf '%s\n' 'app requires Tcl/Tk GUI support; build the CLI with make GUI=no instead.' >&2
	@exit 1
endif

clean:
	rm -rf .build

print-config:
	@printf 'Tcl/Tk GUI: %s\n' '$(TK_ENABLED)'
	@printf 'TCL_CFLAGS: %s\n' '$(TCL_CFLAGS)'
	@printf 'TCL_LIBS: %s\n' '$(TCL_LIBS)'
