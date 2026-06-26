#ifndef __CHARGENCONV_HH_20260626__
#define __CHARGENCONV_HH_20260626__
#include <inttypes.h>
#include <vector>
#include <Magick++.h>


std::vector<uint8_t> get_bitmap(const Magick::Image &img, unsigned x_, unsigned y_);
std::vector<uint8_t> convert_whole_bitmap(const Magick::Image &img);

#endif
