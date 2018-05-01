MAGICK = `pkg-config --cflags --libs Magick++`
CPPFLAGS = -DNDEBUG
CXXFLAGS = -Wall -O2 -Wextra

.PHONY: all
all:	graphconv

graphconv:	graphconv.cc
	$(CXX) -std=c++11 -o $@ $+ $(MAGICK) $(CXXFLAGS) $(CPPFLAGS)

.PHONY: clean
clean:
	rm -f graphconv
