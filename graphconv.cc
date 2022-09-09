#include <cmath>
#include <Magick++.h>
#include <iostream>
#include <cstdio>
#include <cassert>
#include <vector>
#include <iterator>
#include <list>
#include <sstream>
#include <fstream>
#include "change_ending.hh"

struct CharBlock {
  int idx1, idx2;
  std::vector<bool> data;
};

/* Colors taken from Grafx2 */
//double c64colors[][3]
std::vector<Magick::ColorRGB> c64colors {
  {  0.000000e+00,  0.000000e+00,  0.000000e+00 },
  {  1.000000e+00,  1.000000e+00,  1.000000e+00 },
  {  4.078431e-01,  2.156863e-01,  1.686275e-01 },
  {  4.392157e-01,  6.431373e-01,  6.980392e-01 },
  {  4.352941e-01,  2.392157e-01,  5.254902e-01 },
  {  3.450980e-01,  5.529412e-01,  2.627451e-01 },
  {  2.078431e-01,  1.568627e-01,  4.745098e-01 },
  {  7.215686e-01,  7.803922e-01,  4.352941e-01 },
  {  4.352941e-01,  3.098039e-01,  1.450980e-01 },
  {  2.627451e-01,  2.235294e-01,  0.000000e+00 },
  {  6.039216e-01,  4.039216e-01,  3.490196e-01 },
  {  2.666667e-01,  2.666667e-01,  2.666667e-01 },
  {  4.235294e-01,  4.235294e-01,  4.235294e-01 },
  {  6.039216e-01,  8.235294e-01,  5.176471e-01 },
  {  4.235294e-01,  3.686275e-01,  7.098039e-01 },
  {  5.843137e-01,  5.843137e-01,  5.843137e-01 },
} ;


/** \brief calculate the distance between colours

    \param x first colour
    \param y second colour
    \return euclidian distance
 */
double col_dist(const Magick::ColorRGB &x, const Magick::ColorRGB &y) {
  double dist;

  dist = std::sqrt(std::pow(x.red() - y.red(), 2) +
		   std::pow(x.green() - y.green(), 2) +
		   std::pow(x.blue() - y.blue(), 2));
  return dist;
}

void write_char_blocks(const std::list<CharBlock> &blocks, std::ostream &out, unsigned short addr = 0x2000) {
  std::cerr << "Writing blocks #" << blocks.size() << '\n';
  out << static_cast<char>(addr & 0xFF) << static_cast<char>(addr >> 8);
  for(auto &i : blocks) {
    assert(i.data.size() == 64);
    for(auto j = i.data.begin(); j < i.data.end(); j += 8) {
      int x = 0;
      for(int k = 0; k < 8; ++k) {
	x <<= 1;
	x |= j[k] ? 1 : 0;
      }
      assert(x >= 0 && x <= 0xFF);
      out << static_cast<char>(x);
    }
  }
  for(auto &i : blocks) {
    int x = (i.idx1 << 4) | i.idx2;
    out << static_cast<char>(x);
  }
  std::cerr << "Gfx: $" << std::hex << addr << "-$" << std::hex << addr + 320*200/8 - 1 << '\n';
  std::cerr << "Col: $" << std::hex << addr + 320*200/8 << "-$" << std::hex << addr + 320*200/8 + 40*25 << '\n';
}

std::vector<double> calc_distances(Magick::PixelPacket *view, unsigned long size) {
  std::vector<double> ret(16);
  auto f = [&] (Magick::PixelPacket &x) {
      for(int i = 0; i < 16; ++i) {
      }
  };

  for(auto ptr = view; ptr < view + size; ++ptr) {
    std::cout << ptr->green;
  }
  std::for_each(view, view + size, f);
  return ret;
}


std::vector<double> calc_distances(const Magick::Image &img, unsigned x_, unsigned y_, unsigned width, unsigned height) {
  std::vector<double> cdists(16);
  int i;
  unsigned x, y;

  for(y = y_; y < y_ + height; ++y) {
    for(x = x_; x < x_ + width; ++x) {
      Magick::ColorRGB col(img.pixelColor(x, y));
      for(i = 0; i < 16; ++i) {
	cdists[i] = col_dist(col, c64colors[i]);
      }
    }
  }
  return cdists;
}


double calc_distances_2col(const Magick::Image &img, unsigned x_, unsigned y_, unsigned width, unsigned height, int cidx0, int cidx1) {
  unsigned x, y;
  double cdists[2];
  double total = 0;

  assert(cidx0 < 16 && cidx1 < 16);
  for(y = y_; y < y_ + height; ++y) {
    for(x = x_; x < x_ + width; ++x) {
      Magick::ColorRGB col(img.pixelColor(x, y));
      //assert(col.red() <= 1 && col.green() <= 1 && col.blue() <= 1);
      cdists[0] = col_dist(col, c64colors[cidx0]);
      cdists[1] = col_dist(col, c64colors[cidx1]);
      if(cdists[0] < cdists[1]) {
	total += cdists[0];
      } else {
	total += cdists[1];
      }
#if 0
      std::cout << "calc_distances_2col(" << cidx0 << ',' << cidx1 << ") " << x << ',' << y << '\t' << cdists[0] << ' ' << cdists[1] << "\t𝚺" << total << std::endl;
#endif
    }
  }
  return total;
}


std::vector<bool> get_bitmap(const Magick::Image &img, unsigned x_, unsigned y_, int cidx0, int cidx1) {
  unsigned x, y;
  double cdists[2];
  std::vector<bool> ret;

  for(y = y_; y < y_ + 8; ++y) {
    for(x = x_; x < x_ + 8; ++x) {
      Magick::ColorRGB col(img.pixelColor(x, y));
      cdists[0] = col_dist(col, c64colors[cidx0]);
      cdists[1] = col_dist(col, c64colors[cidx1]);
      ret.push_back(cdists[0] < cdists[1]);
    }
  }
  return ret;
}

double replace_color_in_block(Magick::Image &img, unsigned x_, unsigned y_, unsigned width, unsigned height, int cidx0, int cidx1) {
  unsigned x, y;
  double cdists[2];
  int idx;
  double total = 0;

  for(y = y_; y < y_ + height; ++y) {
    for(x = x_; x < x_ + width; ++x) {
      Magick::ColorRGB col(img.pixelColor(x, y));
      cdists[0] = col_dist(col, c64colors[cidx0]);
      cdists[1] = col_dist(col, c64colors[cidx1]);
      if(cdists[0] < cdists[1]) {
	idx = cidx0;
	total += cdists[0];
      } else {
	idx = cidx1;
	total += cdists[1];
      }
      col = c64colors[idx];
      img.pixelColor(x, y, col);
    }
  }
  return total;
}


void handle_block_wise(Magick::Image &img) {
  unsigned x, y;
  std::vector<double> cdists(16);
  int idx1, idx2;

  for(y = 0; y < img.rows() - 8; y += 8) {
    for(x = 0; x < img.columns() - 8; x += 8) {
      cdists = calc_distances(img, x, y, 8, 8);
      /* Find the *index* of the two lowest elements. */
      auto min1 = std::min_element(cdists.begin(), cdists.end());
      *min1 = NAN;
      auto min2 = std::min_element(cdists.begin(), cdists.end());
      idx1 = std::distance(cdists.begin(), min1);
      idx2 = std::distance(cdists.begin(), min2);

      std::cout << x << ' ' << y << '\t' << idx1 << ' ' << idx2;
      assert(idx1 < 16 && idx1 >= 0);
      assert(idx2 < 16 && idx2 >= 0);
      std::cout << replace_color_in_block(img, x, y, 8, 8, idx1, idx2);
      std::cout << std::endl;
    }
  }
}

std::list<CharBlock> handle_block_wise2(Magick::Image &img) {
  unsigned x, y;
  std::list<double> cdists;
  std::list<CharBlock> blocks;

  for(y = 0; y < img.rows(); y += 8) {
    for(x = 0; x < img.columns(); x += 8) {
      double err = 69105;
      int idx1 = -1, idx2 = -1;
      for(int i = 0; i < 16; ++i) {
	for(int j = i + 1; j < 16; ++j) {
	  double e = calc_distances_2col(img, x, y, 8, 8, i, j);
	  if(err > e) {
	    idx1 = i;
	    idx2 = j;
	    err = e;
	  }
	}
      }
      assert(idx1 >= 0 && idx2 >= 0);
      cdists.push_back(replace_color_in_block(img, x, y, 8, 8, idx1, idx2));
      CharBlock block = { idx1, idx2, get_bitmap(img, x, y, idx1, idx2) };
      blocks.push_back(block);
      // std::cout << x << ' ' << y << '\t' << idx1 << ' ' << idx2 << ' ';
      // std::cout << std::endl;
    }
  }
  return blocks;
}

void handle_image(Magick::Image &img, unsigned x_, unsigned y_, unsigned width, unsigned height) {
  int i;
  double dist;
  unsigned x, y;
  double cdists[16], *dptr;

  img.type(Magick::TrueColorType);
  Magick::ColorRGB col(img.pixelColor(0,0));
  std::cout << col.red() << ' ' << col.green() << ' ' << col.blue() << std::endl;
  for(i = 0; i < 16; ++i) {
    dist = col_dist(col, c64colors[i]);
    printf("%02X %13.6e %13.6e %13.6e %13.6E\n", i,
	   std::abs(col.red() - c64colors[i].red()),
	   std::abs(col.green() - c64colors[i].green()),
	   std::abs(col.blue() - c64colors[i].blue()),
	   dist
	   );
  }
  for(y = y_; y < y_ + height; ++y) {
    for(x = x_; x < x_ + width; ++x) {
      Magick::ColorRGB col(img.pixelColor(x, y));
      for(i = 0; i < 16; ++i) {
	cdists[i] = col_dist(col, c64colors[i]);
      }
      dptr = std::min_element(cdists, cdists + 16);
      i = std::distance(cdists, dptr);
      assert((i >= 0) && (i < 16));
      col = c64colors[i];
      img.pixelColor(x, y, col);
    }
  }
}

int main(int argc, char **argv) {
  if(argc < 2) {
    std::cerr << "Usage: graphconv <file>\n";
    return 1;
  } else {
    Magick::Image img(argv[1]);
    img.crop(Magick::Geometry(320, 200, 0, 0));
    if(img.columns() < 320 || img.rows() < 200) {
      std::ostringstream out;
      out << "wrong picture size (" << img.columns() << '*' << img.rows() << ')';
      throw std::invalid_argument(out.str());
    }
    /*
     * Convert the image to a true type image so that the following
     * pixel methods work correctly as we assume an RGB image. See
     * also https://www.imagemagick.org/Magick++/Pixels.html.
     */
    img.type(Magick::TrueColorType);
    img.display();
    std::list<CharBlock> blocks(handle_block_wise2(img));
    // handle_image(img, 0, 0, img.columns(), img.rows());
    // img.display();
    img.write(change_ending(argv[1], "ilbm"));
    img.write(change_ending(argv[1], "xpm"));
    std::ofstream outfile(change_ending(argv[1], "c64"));
    write_char_blocks(blocks, outfile);
    img.display();
  }
  return 0;
}
