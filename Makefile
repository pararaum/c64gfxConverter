MAGICK = $(shell pkg-config --cflags --libs Magick++)
CPPFLAGS = -DNDEBUG
CXXFLAGS = -Wall -O2 -Wextra -std=c++14

BIN = graphconv spriteconv petscii80x50


.PHONY: all
all:	$(BIN)

petscii80x50:	petscii80x50.cc
	$(CXX) -std=c++14 -o $@ $+ $(MAGICK) $(CXXFLAGS) $(CPPFLAGS)

graphconv:	graphconv.cc
	$(CXX) -std=c++14 -o $@ $+ $(MAGICK) $(CXXFLAGS) $(CPPFLAGS)

spriteconv:	spriteconv_cli.o spriteconv.o
	$(CXX) -std=c++14 -o $@ $+ $(CXXFLAGS) $(CPPFLAGS) -lSDL2 -lSDL2_image

spriteconv_cli.c:	spriteconv_cli.ggo
	gengetopt -i $< -F spriteconv_cli -u

spriteconv_cli.o: spriteconv_cli.c spriteconv_cli.ggo

.PHONY: clean
clean:
	rm -f $(BIN) *.o
	rm -f *_cli.c *_cli.h
