#! /usr/bin/make -f

CXX = g++ -std=c++17
MAGICK = $(shell pkg-config --cflags --libs Magick++)
CPPFLAGS = -DNDEBUG
CXXFLAGS = -Wall -O2 -Wextra $(shell pkg-config --cflags Magick++)

BIN = graphconv spriteconv petscii80x50 chargenconv


.PHONY: all
all:	$(BIN)

petscii80x50:	petscii80x50.cc
	$(CXX) -o $@ $+ $(MAGICK) $(CXXFLAGS) $(CPPFLAGS)

graphconv:	graphconv.o change_ending.o
	$(CXX) -o $@ $+ $(MAGICK) $(CXXFLAGS) $(CPPFLAGS)

chargenconv:	chargenconv.o change_ending.o
	$(CXX) -o $@ $+ $(MAGICK) $(CXXFLAGS) $(CPPFLAGS)

spriteconv:	spriteconv_cli.o spriteconv.o
	$(CXX) -o $@ $+ $(CXXFLAGS) $(CPPFLAGS) -lSDL2 -lSDL2_image

spriteconv_cli.c:	spriteconv_cli.ggo
	gengetopt -i $< -F spriteconv_cli -u

spriteconv_cli.o: spriteconv_cli.c spriteconv_cli.ggo

.PHONY: clean install
clean:
	rm -f $(BIN) *.o
	rm -f *_cli.c *_cli.h

install:
	install -d $(DESTDIR)/usr/local/bin
	install $(BIN) $(DESTDIR)/usr/local/bin
