# =============================================================================
# MyGit Makefile
# Supports: GCC, Clang (Linux/macOS/MinGW), MSVC (via nmake)
# =============================================================================

# --- Toolchain ---------------------------------------------------------------
CC      ?= gcc
TARGET  ?= mygit

# --- Sources -----------------------------------------------------------------
SRCS    := mygit.c cli.c repo.c fs_utils.c
OBJS    := $(SRCS:.c=.o)
DEPS    := mygit.h repo.h cli.h fs_utils.h

# --- Flags -------------------------------------------------------------------
CFLAGS_COMMON := -std=c11 \
                 -Wall -Wextra -Wpedantic \
                 -Wshadow -Wformat=2 \
                 -Wnull-dereference \
                 -Wdouble-promotion \
                 -Wmissing-prototypes \
                 -Wstrict-prototypes

CFLAGS_DEBUG   := $(CFLAGS_COMMON) -g -O0 -DDEBUG
CFLAGS_RELEASE := $(CFLAGS_COMMON) -O2 -DNDEBUG

# Default build is debug
CFLAGS ?= $(CFLAGS_DEBUG)

# Windows (MinGW): executable gets .exe automatically
ifeq ($(OS), Windows_NT)
    TARGET  := mygit.exe
    RM      := del /Q
else
    RM      := rm -f
endif

# --- Targets -----------------------------------------------------------------
.PHONY: all release clean help

all: $(TARGET)
	@echo ""
	@echo "  Built: $(TARGET)  [debug]"
	@echo "  Run:   ./$(TARGET)"
	@echo ""

release: CFLAGS := $(CFLAGS_RELEASE)
release: clean $(TARGET)
	@echo ""
	@echo "  Built: $(TARGET)  [release, optimised]"
	@echo ""

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(OBJS) mygit mygit.exe 2>/dev/null || true

help:
	@echo "Targets:"
	@echo "  all      Debug build (default)"
	@echo "  release  Optimised build, no debug symbols"
	@echo "  clean    Remove build artefacts"
	@echo ""
	@echo "Variables:"
	@echo "  CC=clang   Use Clang instead of GCC"
	@echo "  CC=gcc     Use GCC (default)"

# =============================================================================
# MSVC / nmake  (run: nmake /f Makefile)
# =============================================================================
# !IF "$(MAKETYPE)"=="nmake"
# CC      = cl
# CFLAGS  = /W4 /O2 /Fe:mygit.exe /std:c11
# SRCS    = mygit.c cli.c repo.c fs_utils.c
# all:
# 	$(CC) $(CFLAGS) $(SRCS)
# clean:
# 	del /Q *.obj mygit.exe
# !ENDIF