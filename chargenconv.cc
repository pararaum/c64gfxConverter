#include <inttypes.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <Magick++.h>
#include "change_ending.hh"

/*
 * chargenconv
 * ===========
 *
 * Convert a bitmap of at least 128 * 128 pixels as we assume that the
 * characters are aligned into a 8 characters times 8 characters
 * matrix.
 */


std::vector<uint8_t> get_bitmap(const Magick::Image &img, unsigned x_, unsigned y_) {
  unsigned x, y;
  std::vector<uint8_t> ret;
  uint8_t val;
  uint8_t mask;

  for(y = y_; y < y_ + 8; ++y) {
    val = 0;
    mask = 0x80;
    for(x = x_; x < x_ + 8; ++x) {
      Magick::ColorMono col(img.pixelColor(x, y));
      if(col.mono()) {
	val |= mask;
      }
      mask >>= 1; // Move mask bit to the right.
    }
    ret.push_back(val);
  }
  return ret;
}


std::vector<uint8_t> convert_whole_bitmap(const Magick::Image &img) {
  std::vector<uint8_t> chargen;

  for(unsigned y = 0; y < 128; y += 8) {
    for(unsigned x = 0; x < 128; x += 8) {
      auto block(get_bitmap(img, x, y));
      std::copy(block.begin(), block.end(), std::back_inserter(chargen));
    }
  }
  return chargen;
}


int main(int argc, char **argv) {
  if(argc < 2) {
    std::cerr << "Usage: chargenconv <file>\n";
    return 1;
  } else {
    Magick::Image img(argv[1]);
    if(img.columns() < 128 || img.rows() < 128) {
      std::ostringstream out;
      out << "wrong picture size (" << img.columns() << '*' << img.rows() << ')';
      throw std::invalid_argument(out.str());
    } else {
      img.crop(Magick::Geometry(128, 128, 0, 0));
    }
    /*
     * Convert the image to a true type image so that the following
     * pixel methods work correctly as we assume an RGB image. See
     * also https://www.imagemagick.org/Magick++/Pixels.html.
     */
    img.threshold(50000);
    img.type(Magick::TrueColorType);
    img.modifyImage();
    //img.type(Magick::BilevelType);
    auto chargen(convert_whole_bitmap(img));
    img.display(); // After display, image data is botched???
    //img.write(change_ending(argv[1], "ilbm"));
    img.write(change_ending(argv[1], "xpm"));
    std::ofstream outfile(change_ending(argv[1], "c64"));
    //std::copy(chargen.begin(), chargen.end(), std::ostream_iterator<int>(std::cout, " "));
    std::copy(chargen.begin(), chargen.end(), std::ostream_iterator<char>(outfile));
  }
  return 0;
}
