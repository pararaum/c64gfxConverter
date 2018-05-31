MAGICK = `pkg-config --cflags --libs Magick++`
CPPFLAGS = -DNDEBUG
CXXFLAGS = -Wall -O2 -Wextra

BIN = graphconv spriteconv

.PHONY: all
all:	$(BIN)

graphconv:	graphconv.cc
	$(CXX) -std=c++11 -o $@ $+ $(MAGICK) $(CXXFLAGS) $(CPPFLAGS)

spriteconv:	spriteconv.o spriteconv_cli.h spriteconv_cli.o
	$(CXX) -std=c++11 -o $@ $+ $(CXXFLAGS) $(CPPFLAGS) -lSDL2 -lSDL2_image

spriteconv_cli.h:	spriteconv_cli.ggo
	gengetopt -i $< -F spriteconv_cli -u

.PHONY: clean
clean:
	rm -f $(BIN) *.o
	rm -f *_cli.c *_cli.h
