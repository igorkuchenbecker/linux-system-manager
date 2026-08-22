# Linux System Manager — build system
#
# Targets:
#   make / make all   -> fast debuggable build (build/dev/), no sanitizers
#   make debug        -> ASan+UBSan build (build/debug/), for bug hunting
#   make release       -> optimized build (build/release/), -O2 -DNDEBUG
#   make test          -> builds and runs the unit test suite
#   make run           -> builds (dev) and runs the CLI/TUI binary
#   make run-daemon    -> builds (dev) and runs the daemon in the foreground
#   make install       -> release build + installs as `linuxmng`/`linuxmngd`
#                          into $(PREFIX)/bin (default: ~/.local/bin, no root)
#   make uninstall     -> removes the installed `linuxmng`/`linuxmngd`
#   make clean         -> removes build/ and bin/
#
# Two binaries share most of their source (every collector module, core,
# utils, ipc): linux-system-manager (CLI/TUI, src/main.c + src/ui/) and
# linux-system-managerd (the daemon, src/daemon/). Both getting their own
# main() means they can never be linked into the same binary, so the
# source list is split below into daemon-only, client-only, and shared —
# shared objects are compiled exactly once per mode and linked into both.
#
# Implementation note: `all`/`debug`/`release` each re-invoke $(MAKE) with
# MODE set on the command line (highest-precedence variable origin in
# GNU Make). This is necessary, not decorative — CFLAGS/BUILD_DIR/OBJ below
# are computed once at parse time, so if MODE were only a target-specific
# variable, the object-file list `all`, `debug` and `release` see would all
# be resolved before any target-specific value took effect, and every mode
# would silently share one build/ directory with whichever flags happened
# to be evaluated first. Recursing with MODE=<x> on the command line forces
# a fresh, correctly-parameterized parse per mode.

CC      ?= gcc
BIN_DIR := bin
BIN        := $(BIN_DIR)/linux-system-manager
DAEMON_BIN := $(BIN_DIR)/linux-system-managerd

STD       := -std=c17
FEATURES  := -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
WARNINGS  := -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align \
             -Wwrite-strings -Wmissing-prototypes -Wstrict-prototypes \
             -Wold-style-definition -Wformat=2
INCLUDES  := -Iinclude

CFLAGS_COMMON  := $(STD) $(FEATURES) $(WARNINGS) $(INCLUDES)
LDLIBS_COMMON  := -lpthread -ldl
LDLIBS_CLIENT  := $(LDLIBS_COMMON) -lncursesw
LDLIBS_DAEMON  := $(LDLIBS_COMMON)

CFLAGS_DEBUG  := $(CFLAGS_COMMON) -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS_DEBUG := -fsanitize=address,undefined

ALL_SRC          := $(shell find src -name '*.c')
DAEMON_ONLY_SRC  := $(shell find src/daemon -name '*.c' 2>/dev/null)
UI_SRC           := $(shell find src/ui -name '*.c' 2>/dev/null)
CLIENT_ONLY_SRC  := src/main.c $(UI_SRC)
SHARED_SRC       := $(filter-out $(DAEMON_ONLY_SRC) $(CLIENT_ONLY_SRC),$(ALL_SRC))

CLIENT_SRC := $(CLIENT_ONLY_SRC) $(SHARED_SRC)
DAEMON_SRC := $(DAEMON_ONLY_SRC) $(SHARED_SRC)

TEST_SRC := $(wildcard tests/*.c)

.PHONY: all debug release build test run run-daemon clean

all:
	@$(MAKE) --no-print-directory MODE=dev build

debug:
	@$(MAKE) --no-print-directory MODE=debug build

release:
	@$(MAKE) --no-print-directory MODE=release build

MODE ?= dev

ifeq ($(MODE),debug)
  CFLAGS  := $(CFLAGS_DEBUG)
  LDFLAGS := $(LDFLAGS_DEBUG)
else ifeq ($(MODE),release)
  CFLAGS  := $(CFLAGS_COMMON) -O2 -DNDEBUG
  LDFLAGS :=
else
  CFLAGS  := $(CFLAGS_COMMON) -g -O0
  LDFLAGS :=
endif

BUILD_DIR  := build/$(MODE)
CLIENT_OBJ = $(CLIENT_SRC:src/%.c=$(BUILD_DIR)/%.o)
DAEMON_OBJ = $(DAEMON_SRC:src/%.c=$(BUILD_DIR)/%.o)

build: $(BIN) $(DAEMON_BIN)

$(BIN): $(CLIENT_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(CLIENT_OBJ) -o $@ $(LDLIBS_CLIENT)

$(DAEMON_BIN): $(DAEMON_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(DAEMON_OBJ) -o $@ $(LDLIBS_DAEMON)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

run: all
	./$(BIN)

run-daemon: all
	./$(DAEMON_BIN)

# --- install -------------------------------------------------------------
# Installs as `linuxmng`/`linuxmngd` — the command names the user runs —
# distinct from the internal build artifact names above (linux-system-manager
# / linux-system-managerd), same as how e.g. ripgrep the project builds an
# `rg` command. Defaults to ~/.local/bin, which needs no root and is
# already on PATH on most modern distros (verified for this project's own
# dev setup); override with `make install PREFIX=/usr/local` for a
# system-wide install (that one needs sudo).
PREFIX  ?= $(HOME)/.local
BINDIR  := $(PREFIX)/bin

.PHONY: install uninstall

install: release
	install -Dm755 $(BIN) $(BINDIR)/linuxmng
	install -Dm755 $(DAEMON_BIN) $(BINDIR)/linuxmngd
	@echo "Installed: $(BINDIR)/linuxmng and $(BINDIR)/linuxmngd"
	@case ":$$PATH:" in \
		*":$(BINDIR):"*) ;; \
		*) echo "NOTE: $(BINDIR) is not on your current PATH — open a new shell or add it."; ;; \
	esac

uninstall:
	rm -f $(BINDIR)/linuxmng $(BINDIR)/linuxmngd

# --- tests -------------------------------------------------------------
# Every tests/*.c is a standalone program (its own main()) linked against
# every shared project object (collectors, core, utils, ipc) except the
# two real main()s (src/main.c and src/daemon/main.c) and the UI, which
# the test suite does not (yet) exercise directly. Always built in
# ASan+UBSan mode so the unit test suite doubles as a memory-safety check.

TEST_BUILD_DIR := build/test
TEST_LIB_SRC   := $(SHARED_SRC)
TEST_LIB_OBJ   := $(TEST_LIB_SRC:src/%.c=$(TEST_BUILD_DIR)/%.o)
TEST_BINS      := $(TEST_SRC:tests/%.c=$(TEST_BUILD_DIR)/%)

$(TEST_BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DEBUG) -c $< -o $@

$(TEST_BUILD_DIR)/%: tests/%.c $(TEST_LIB_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DEBUG) $(LDFLAGS_DEBUG) $< $(TEST_LIB_OBJ) -o $@ $(LDLIBS_COMMON)

test: $(TEST_BINS)
	@status=0; \
	for t in $(TEST_BINS); do \
		echo "-- running $$t --"; \
		./$$t || status=1; \
	done; \
	exit $$status

clean:
	rm -rf build $(BIN_DIR)
