#include "chargenconv.hh"

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

