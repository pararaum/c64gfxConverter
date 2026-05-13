#! /usr/bin/make -f

# ── suite version (CalVer: YYYY.N) ────────────────────────────────────────────
# Bump the release counter (N) on each release within a year.
# Passed to every translation unit as -DSUITE_VERSION="YYYY.N".
VERSION := 2026.5

# ── install prefix ────────────────────────────────────────────────────────────
PREFIX  ?= /usr/local

# ── compiler ──────────────────────────────────────────────────────────────────
# CXX must be the bare compiler name; language-standard flags belong in CXXFLAGS
# so that implicit rules, dependency generation, and external tools all work.
CXX      = g++

# ── pkg-config: split compile flags from link flags ───────────────────────────
# Using separate variables avoids duplicating -I paths on link lines and
# avoids mixing -l flags into compile-only invocations.
MAGICK_CFLAGS := $(shell pkg-config --cflags Magick++)
MAGICK_LIBS   := $(shell pkg-config --libs   Magick++)

SDL_CFLAGS    := $(shell pkg-config --cflags sdl2 SDL2_image 2>/dev/null)
SDL_LIBS      := $(shell pkg-config --libs   sdl2 SDL2_image 2>/dev/null || echo -lSDL2 -lSDL2_image)

# ── flags ─────────────────────────────────────────────────────────────────────
# CPPFLAGS: preprocessor (seen by both C and C++ sources, and by the linker)
# CXXFLAGS: C++-compiler options only (not passed to the linker)
# LDFLAGS:  linker options (search paths, rpath, etc.)
# LDLIBS:   libraries (appended after object files by implicit link rules)
CPPFLAGS  = -DNDEBUG -DSUITE_VERSION=\"$(VERSION)\"
CXXFLAGS  = -std=c++23 -Wall -Wextra -O2 \
             $(MAGICK_CFLAGS) $(SDL_CFLAGS)
LDFLAGS   =
LDLIBS    =

# ── dependency tracking ───────────────────────────────────────────────────────
# -MMD generates a .d file alongside each .o with header dependencies.
# -MP adds phony targets for headers so a deleted header doesn't break make.
DEPFLAGS  = -MMD -MP
CXXFLAGS += $(DEPFLAGS)

# ── targets ───────────────────────────────────────────────────────────────────
BIN = graphconv spriteconv petscii80x50 chargenconv petsciiconvert

.PHONY: all
all: $(BIN)

# petscii80x50 builds via an intermediate .o like the other targets.
petscii80x50: petscii80x50.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(MAGICK_LIBS)

graphconv: graphconv.o change_ending.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(MAGICK_LIBS)

chargenconv: chargenconv.o change_ending.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(MAGICK_LIBS)

spriteconv: spriteconv.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL_LIBS)

# ── gengetopt-generated sources ───────────────────────────────────────────────
petsciiconvert_cli.c: petsciiconvert_cli.ggo
	gengetopt -i $< -F $(basename $@) -u

# Explicit prerequisite so the generated header is rebuilt before its users.
petsciiconvert_cli.o: petsciiconvert_cli.c petsciiconvert_cli.h

petsciiconvert: petsciiconvert_cli.o parse-petsciifile.o compare_frames.o petsciiconvert.o
	$(CXX) $(LDFLAGS) -o $@ $^

# ── include generated dependency files ───────────────────────────────────────
# The leading dash suppresses errors when .d files don't exist yet (first build).
-include $(wildcard *.d)

# ── housekeeping ──────────────────────────────────────────────────────────────
.PHONY: clean install

clean:
	rm -f $(BIN) *.o *.d
	rm -f *_cli.c *_cli.h

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin
