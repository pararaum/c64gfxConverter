/**
 * \file graphconv.cc
 * \brief Convert an image to a Commodore 64 hires bitmap format.
 *
 * Loads an image (via ImageMagick/Magick++), crops it to 320×200 pixels,
 * quantises every 8×8 pixel block to the two best-matching C64 palette
 * colours, and writes the result as a raw C64 bitmap file (.c64).
 * Optionally the dithered image can also be saved as ILBM and/or XPM.
 *
 * Build dependencies: Magick++, CLI11
 */

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
#include <CLI/CLI.hpp>
#include "change_ending.hh"

/**
 * \brief Represents one 8×8 character block with its two palette colours.
 *
 * \c idx1 and \c idx2 are indices into \c c64colors (0–15).
 * \c data holds the 64 per-pixel colour decisions as booleans:
 *   \c false → colour \c idx1, \c true → colour \c idx2.
 */
struct CharBlock {
  int idx1, idx2;        ///< Indices of the two C64 colours used in this block
  std::vector<bool> data; ///< Per-pixel colour assignment (64 elements)
};

/**
 * \brief The 16-colour Commodore 64 palette in normalised RGB (0.0–1.0).
 *
 * Colour values are taken from the Grafx2 palette definition.
 * Order: black, white, red, cyan, purple, green, blue, yellow,
 *        orange, brown, light red, dark grey, grey, light green,
 *        light blue, light grey.
 */
std::vector<Magick::ColorRGB> c64colors {
  {  0.000000e+00,  0.000000e+00,  0.000000e+00 }, ///< 0  black
  {  1.000000e+00,  1.000000e+00,  1.000000e+00 }, ///< 1  white
  {  4.078431e-01,  2.156863e-01,  1.686275e-01 }, ///< 2  red
  {  4.392157e-01,  6.431373e-01,  6.980392e-01 }, ///< 3  cyan
  {  4.352941e-01,  2.392157e-01,  5.254902e-01 }, ///< 4  purple
  {  3.450980e-01,  5.529412e-01,  2.627451e-01 }, ///< 5  green
  {  2.078431e-01,  1.568627e-01,  4.745098e-01 }, ///< 6  blue
  {  7.215686e-01,  7.803922e-01,  4.352941e-01 }, ///< 7  yellow
  {  4.352941e-01,  3.098039e-01,  1.450980e-01 }, ///< 8  orange
  {  2.627451e-01,  2.235294e-01,  0.000000e+00 }, ///< 9  brown
  {  6.039216e-01,  4.039216e-01,  3.490196e-01 }, ///< 10 light red
  {  2.666667e-01,  2.666667e-01,  2.666667e-01 }, ///< 11 dark grey
  {  4.235294e-01,  4.235294e-01,  4.235294e-01 }, ///< 12 grey
  {  6.039216e-01,  8.235294e-01,  5.176471e-01 }, ///< 13 light green
  {  4.235294e-01,  3.686275e-01,  7.098039e-01 }, ///< 14 light blue
  {  5.843137e-01,  5.843137e-01,  5.843137e-01 }, ///< 15 light grey
};


/**
 * \brief Compute the Euclidean distance between two RGB colours.
 *
 * Operates in the normalised [0, 1]³ colour cube.
 *
 * \param x First colour.
 * \param y Second colour.
 * \return Euclidean distance between \p x and \p y.
 */
double col_dist(const Magick::ColorRGB &x, const Magick::ColorRGB &y) {
  double dist;

  dist = std::sqrt(std::pow(x.red()   - y.red(),   2) +
                   std::pow(x.green() - y.green(), 2) +
                   std::pow(x.blue()  - y.blue(),  2));
  return dist;
}

/**
 * \brief Serialise a list of CharBlocks to a raw C64 bitmap stream.
 *
 * The output layout matches the C64 hires bitmap format:
 *   - 2-byte little-endian load address
 *   - Bitmap data: 8 bytes per block (one bit per pixel), blocks in
 *     left-to-right, top-to-bottom order (320×200 / 8 = 8000 bytes)
 *   - Colour nybble data: one byte per block, high nybble = idx1,
 *     low nybble = idx2 (40×25 = 1000 bytes)
 *
 * \param blocks  List of CharBlocks covering the full 320×200 image.
 * \param out     Destination output stream (binary).
 * \param addr    C64 load address written as the 2-byte header (default 0x2000).
 */
void write_char_blocks(const std::list<CharBlock> &blocks, std::ostream &out, unsigned short addr = 0x2000) {
  std::cerr << "Writing blocks #" << blocks.size() << '\n';

  /* Write little-endian load address */
  out << static_cast<char>(addr & 0xFF) << static_cast<char>(addr >> 8);

  /* Write bitmap data: pack each row of 8 pixels into one byte */
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

  /* Write colour attribute data: one byte per block (idx1 | idx2) */
  for(auto &i : blocks) {
    int x = (i.idx1 << 4) | i.idx2;
    out << static_cast<char>(x);
  }

  std::cerr << "Gfx: $" << std::hex << addr << "-$" << std::hex << addr + 320*200/8 - 1 << '\n';
  std::cerr << "Col: $" << std::hex << addr + 320*200/8 << "-$" << std::hex << addr + 320*200/8 + 40*25 << '\n';
}


/**
 * \brief Compute per-palette-entry colour distances for every pixel in a region.
 *
 * Iterates over each pixel in the rectangle [\p x_, \p x_+\p width) ×
 * [\p y_, \p y_+\p height) and accumulates the distance to each of the
 * 16 C64 palette entries.
 *
 * \note The returned vector contains the distance accumulated from the
 *       \e last pixel only (the inner loop overwrites rather than sums).
 *       This function is used by the legacy \c handle_block_wise path.
 *
 * \param img     Source image (read-only).
 * \param x_      Left edge of the region.
 * \param y_      Top edge of the region.
 * \param width   Width of the region in pixels.
 * \param height  Height of the region in pixels.
 * \return Vector of 16 distances, one per C64 palette entry.
 */
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


/**
 * \brief Compute the total quantisation error for a block restricted to two colours.
 *
 * For every pixel in the rectangle [\p x_, \p x_+8) × [\p y_, \p y_+8)
 * the distance to the closer of the two specified palette entries is
 * accumulated and returned as the total error.
 *
 * \param img     Source image (read-only).
 * \param x_      Left edge of the 8×8 block.
 * \param y_      Top edge of the 8×8 block.
 * \param width   Block width (typically 8).
 * \param height  Block height (typically 8).
 * \param cidx0   First palette index (0–15).
 * \param cidx1   Second palette index (0–15).
 * \return Sum of minimum distances over all pixels in the block.
 */
double calc_distances_2col(const Magick::Image &img, unsigned x_, unsigned y_, unsigned width, unsigned height, int cidx0, int cidx1) {
  unsigned x, y;
  double cdists[2];
  double total = 0;

  assert(cidx0 < 16 && cidx1 < 16);
  for(y = y_; y < y_ + height; ++y) {
    for(x = x_; x < x_ + width; ++x) {
      Magick::ColorRGB col(img.pixelColor(x, y));
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


/**
 * \brief Build a 64-element bitmap for an 8×8 block given two palette colours.
 *
 * Each element is \c false if the pixel is closer to \c cidx0, or \c true
 * if it is closer to \c cidx1.  The result is stored in row-major order.
 *
 * \param img    Source image (read-only).
 * \param x_     Left edge of the 8×8 block.
 * \param y_     Top edge of the 8×8 block.
 * \param cidx0  Palette index for the "0" bit colour.
 * \param cidx1  Palette index for the "1" bit colour.
 * \return 64-element boolean vector (8 rows × 8 columns).
 */
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

/**
 * \brief Quantise an image region to two palette colours in-place.
 *
 * Replaces every pixel in [\p x_, \p x_+\p width) × [\p y_, \p y_+\p height)
 * with whichever of the two specified C64 colours is closer.
 *
 * \param img     Image to modify.
 * \param x_      Left edge of the region.
 * \param y_      Top edge of the region.
 * \param width   Region width in pixels.
 * \param height  Region height in pixels.
 * \param cidx0   First palette index.
 * \param cidx1   Second palette index.
 * \return Total quantisation error (sum of minimum distances).
 */
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


/**
 * \brief Legacy block-wise quantisation (approximate, uses last-pixel distance).
 *
 * Iterates over the image in 8×8 blocks, picks the two palette colours with
 * the lowest per-block distance (using the approximate \c calc_distances),
 * and quantises each block.  Kept for reference; the preferred path is
 * \c handle_block_wise2.
 *
 * \param img  Image to process in-place.
 */
void handle_block_wise(Magick::Image &img) {
  unsigned x, y;
  std::vector<double> cdists(16);
  int idx1, idx2;

  for(y = 0; y < img.rows() - 8; y += 8) {
    for(x = 0; x < img.columns() - 8; x += 8) {
      cdists = calc_distances(img, x, y, 8, 8);
      /* Find the indices of the two smallest distance values */
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

/**
 * \brief Optimal block-wise quantisation by exhaustive two-colour search.
 *
 * For each 8×8 block, tries all C(16,2) = 120 colour pairs and selects
 * the pair that minimises the total quantisation error.  The image is
 * modified in-place and the resulting CharBlock descriptors are returned.
 *
 * \param img  Image to process in-place (must be 320×200, TrueColorType).
 * \return List of CharBlocks in left-to-right, top-to-bottom order.
 */
std::list<CharBlock> handle_block_wise2(Magick::Image &img) {
  unsigned x, y;
  std::list<double> cdists;
  std::list<CharBlock> blocks;

  for(y = 0; y < img.rows(); y += 8) {
    for(x = 0; x < img.columns(); x += 8) {
      double err = 69105;  /* Sentinel: larger than any real error */
      int idx1 = -1, idx2 = -1;

      /* Exhaustive search over all 120 unordered colour pairs */
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
    }
  }
  return blocks;
}

/**
 * \brief Diagnostic full-image quantisation to the nearest single C64 colour.
 *
 * Prints the colour of pixel (0,0) and its per-palette distances, then
 * replaces every pixel in the specified region with its nearest palette colour.
 * Used for debugging / inspection only.
 *
 * \param img     Image to process in-place.
 * \param x_      Left edge of the region.
 * \param y_      Top edge of the region.
 * \param width   Region width in pixels.
 * \param height  Region height in pixels.
 */
void handle_image(Magick::Image &img, unsigned x_, unsigned y_, unsigned width, unsigned height) {
  int i;
  double dist;
  unsigned x, y;
  double cdists[16], *dptr;

  img.type(Magick::TrueColorType);
  Magick::ColorRGB col(img.pixelColor(0, 0));
  std::cout << col.red() << ' ' << col.green() << ' ' << col.blue() << std::endl;
  for(i = 0; i < 16; ++i) {
    dist = col_dist(col, c64colors[i]);
    printf("%02X %13.6e %13.6e %13.6e %13.6E\n", i,
           std::abs(col.red()   - c64colors[i].red()),
           std::abs(col.green() - c64colors[i].green()),
           std::abs(col.blue()  - c64colors[i].blue()),
           dist);
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

/**
 * \brief Program entry point.
 *
 * Parses command-line options with CLI11, loads the input image, crops it
 * to 320×200, runs the optimal two-colour block quantisation, and writes
 * the C64 binary output.  Optionally also writes ILBM and/or XPM files.
 *
 * \param argc  Argument count.
 * \param argv  Argument vector.
 * \return 0 on success, non-zero on error.
 */
int main(int argc, char **argv) {
  CLI::App app{"graphconv – convert an image to C64 hires bitmap format"};

  std::string input_file;
  bool write_ilbm = false;
  bool write_xpm  = false;

  /* Positional argument: the image file to convert */
  app.add_option("file", input_file, "Input image file to convert")
     ->required()
     ->check(CLI::ExistingFile);

  /* Optional output format switches */
  app.add_flag("--write-ilbm", write_ilbm, "Also save the quantised image as ILBM");
  app.add_flag("--write-xpm",  write_xpm,  "Also save the quantised image as XPM");

  CLI11_PARSE(app, argc, argv);

  Magick::Image img(input_file);
  img.crop(Magick::Geometry(320, 200, 0, 0));

  if(img.columns() < 320 || img.rows() < 200) {
    std::ostringstream out;
    out << "wrong picture size (" << img.columns() << '*' << img.rows() << ')';
    throw std::invalid_argument(out.str());
  }

  /*
   * Convert the image to a true colour image so that the following
   * pixel methods work correctly, as we assume an RGB image throughout.
   * See also https://www.imagemagick.org/Magick++/Pixels.html.
   */
  img.type(Magick::TrueColorType);
  img.display();

  /* Run the optimal two-colour quantisation on all 8×8 blocks */
  std::list<CharBlock> blocks(handle_block_wise2(img));

  /* Conditionally write optional image formats */
  if(write_ilbm) {
    img.write(change_ending(input_file, "ilbm"));
  }
  if(write_xpm) {
    img.write(change_ending(input_file, "xpm"));
  }

  /* Always write the native C64 binary output */
  std::ofstream outfile(change_ending(input_file, "c64"));
  write_char_blocks(blocks, outfile);

  img.display();
  return 0;
}
