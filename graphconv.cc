/**
 * \file graphconv.cc
 * \brief Convert an image to a Commodore 64 hires bitmap format.
 *
 * Loads an image (via ImageMagick/Magick++), crops it to 320×200 pixels,
 * quantises every 8×8 pixel block to the two best-matching C64 palette
 * colours, and writes the result as a raw C64 bitmap file (.c64).
 * Optionally the quantised image can also be saved as ILBM and/or XPM.
 *
 * Build dependencies: Magick++, CLI11
 * Requires: C++23 (-std=c++23)
 */

#include <array>
#include <cassert>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <list>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <Magick++.h>
#include <CLI/CLI.hpp>

#include "change_ending.hh"

// ── compile-time constants ────────────────────────────────────────────────────

inline constexpr unsigned IMG_W   = 320; ///< C64 hires bitmap width in pixels
inline constexpr unsigned IMG_H   = 200; ///< C64 hires bitmap height in pixels
inline constexpr unsigned BLK     = 8;   ///< Character block side length in pixels
inline constexpr int      NCOLORS = 16;  ///< Number of C64 palette entries

// ── C64 palette ───────────────────────────────────────────────────────────────

/**
 * \brief Symbolic names for the 16 C64 palette entries.
 *
 * Used as typed indices into \c c64palette to avoid bare magic integers.
 */
enum class C64Color : int {
  Black = 0, White, Red, Cyan, Purple, Green, Blue, Yellow,
  Orange, Brown, LightRed, DarkGrey, Grey, LightGreen, LightBlue, LightGrey
};

/**
 * \brief The 16-colour Commodore 64 palette in normalised RGB (0.0–1.0).
 *
 * Values are taken from the Grafx2 palette definition.
 * Indexed by casting a \c C64Color to \c int, or by a plain integer in [0, 15].
 */
constexpr std::array<std::array<double, 3>, NCOLORS> c64palette {{
  {{ 0.000000e+00, 0.000000e+00, 0.000000e+00 }}, ///< 0  Black
  {{ 1.000000e+00, 1.000000e+00, 1.000000e+00 }}, ///< 1  White
  {{ 4.078431e-01, 2.156863e-01, 1.686275e-01 }}, ///< 2  Red
  {{ 4.392157e-01, 6.431373e-01, 6.980392e-01 }}, ///< 3  Cyan
  {{ 4.352941e-01, 2.392157e-01, 5.254902e-01 }}, ///< 4  Purple
  {{ 3.450980e-01, 5.529412e-01, 2.627451e-01 }}, ///< 5  Green
  {{ 2.078431e-01, 1.568627e-01, 4.745098e-01 }}, ///< 6  Blue
  {{ 7.215686e-01, 7.803922e-01, 4.352941e-01 }}, ///< 7  Yellow
  {{ 4.352941e-01, 3.098039e-01, 1.450980e-01 }}, ///< 8  Orange
  {{ 2.627451e-01, 2.235294e-01, 0.000000e+00 }}, ///< 9  Brown
  {{ 6.039216e-01, 4.039216e-01, 3.490196e-01 }}, ///< 10 LightRed
  {{ 2.666667e-01, 2.666667e-01, 2.666667e-01 }}, ///< 11 DarkGrey
  {{ 4.235294e-01, 4.235294e-01, 4.235294e-01 }}, ///< 12 Grey
  {{ 6.039216e-01, 8.235294e-01, 5.176471e-01 }}, ///< 13 LightGreen
  {{ 4.235294e-01, 3.686275e-01, 7.098039e-01 }}, ///< 14 LightBlue
  {{ 5.843137e-01, 5.843137e-01, 5.843137e-01 }}, ///< 15 LightGrey
}};

/// Construct a Magick++ colour for palette index \p idx.
[[nodiscard]] inline Magick::ColorRGB palette_color(int idx) noexcept {
  const auto &c = c64palette[idx];
  return { c[0], c[1], c[2] };
}

// ── data types ────────────────────────────────────────────────────────────────

/**
 * \brief One 8×8 character block with its two palette colours.
 *
 * \c idx1 and \c idx2 index into \c c64palette (0–15).
 * \c data holds 64 per-pixel colour decisions in row-major order:
 *   \c false → colour \c idx1, \c true → colour \c idx2.
 */
struct CharBlock {
  int idx1, idx2;         ///< Palette indices for the bit-0 and bit-1 colours
  std::vector<bool> data; ///< Per-pixel colour assignment (exactly 64 elements)
};

// ── colour distance ───────────────────────────────────────────────────────────

/**
 * \brief Euclidean distance between two RGB colours in the [0,1]³ cube.
 *
 * \param a First colour.
 * \param b Second colour.
 * \return Non-negative Euclidean distance.
 */
[[nodiscard]] double col_dist(const Magick::ColorRGB &a,
                              const Magick::ColorRGB &b) noexcept
{
  return std::sqrt(std::pow(a.red()   - b.red(),   2)
                 + std::pow(a.green() - b.green(), 2)
                 + std::pow(a.blue()  - b.blue(),  2));
}

/**
 * \brief Index of the palette entry closest to \p col.
 *
 * \param col  Query colour.
 * \return Index in [0, NCOLORS) minimising Euclidean distance to \p col.
 */
[[nodiscard]] int nearest_color(const Magick::ColorRGB &col) noexcept {
  auto idxs = std::views::iota(0, NCOLORS);
  return *std::ranges::min_element(idxs, {},
           [&](int i) { return col_dist(col, palette_color(i)); });
}

// ── C64 binary output ─────────────────────────────────────────────────────────

/**
 * \brief Serialise a list of CharBlocks to a raw C64 bitmap stream.
 *
 * Output layout:
 *   - 2-byte little-endian load address
 *   - Bitmap section: 8 bytes per block, one bit per pixel,
 *     left-to-right / top-to-bottom  (320×200÷8 = 8 000 bytes)
 *   - Colour attribute section: one byte per block,
 *     high nybble = idx1, low nybble = idx2  (40×25 = 1 000 bytes)
 *
 * \param blocks  Blocks covering the full 320×200 image.
 * \param out     Output stream opened in binary mode.
 * \param addr    C64 load address for the 2-byte header (default 0x2000).
 */
void write_char_blocks(const std::list<CharBlock> &blocks,
                       std::ostream               &out,
                       unsigned short              addr = 0x2000)
{
  std::cerr << std::format("Writing {} blocks\n", blocks.size());

  /* Little-endian load address */
  out << static_cast<char>(addr & 0xFF) << static_cast<char>(addr >> 8);

  /* Bitmap section — pack BLK booleans into one byte per pixel row */
  for (const auto &blk : blocks) {
    assert(blk.data.size() == BLK * BLK);
    for (auto row : blk.data | std::views::chunk(BLK)) {
      uint8_t byte = 0;
      for (bool bit : row)
        byte = static_cast<uint8_t>((byte << 1) | (bit ? 1u : 0u));
      out << static_cast<char>(byte);
    }
  }

  /* Colour attribute section: one byte per block */
  for (const auto &blk : blocks)
    out << static_cast<char>((blk.idx1 << 4) | blk.idx2);

  constexpr unsigned gfx_bytes = IMG_W * IMG_H / BLK;
  std::cerr << std::format("Gfx: ${:04X}-${:04X}\n",
                           addr, addr + gfx_bytes - 1);
  std::cerr << std::format("Col: ${:04X}-${:04X}\n",
                           addr + gfx_bytes, addr + gfx_bytes + 40u*25u);
}

// ── block quantisation ────────────────────────────────────────────────────────

/**
 * \brief Total quantisation error when a block is restricted to two colours.
 *
 * For each pixel the distance to the closer of the two palette entries is
 * accumulated.
 *
 * \param img    Source image (read-only).
 * \param x_     Left edge of the block.
 * \param y_     Top edge of the block.
 * \param cidx0  First palette index (0–15).
 * \param cidx1  Second palette index (0–15).
 * \return Sum of per-pixel minimum distances (lower = better fit).
 */
[[nodiscard]] double block_error(const Magick::Image &img,
                                 unsigned x_, unsigned y_,
                                 int cidx0, int cidx1) noexcept
{
  assert(cidx0 < NCOLORS && cidx1 < NCOLORS);
  const auto col0 = palette_color(cidx0);
  const auto col1 = palette_color(cidx1);
  double total = 0.0;

  for (unsigned dy = 0; dy < BLK; ++dy)
    for (unsigned dx = 0; dx < BLK; ++dx) {
      const Magick::ColorRGB col(img.pixelColor(x_ + dx, y_ + dy));
      total += std::min(col_dist(col, col0), col_dist(col, col1));
    }
  return total;
}

/**
 * \brief Quantise an 8×8 block in-place and return its bitmap + error.
 *
 * Each pixel is replaced by whichever of \p cidx0 / \p cidx1 is nearer.
 *
 * \param img    Image to modify.
 * \param x_     Left edge of the block.
 * \param y_     Top edge of the block.
 * \param cidx0  Palette index for the "0" (false) bit.
 * \param cidx1  Palette index for the "1" (true) bit.
 * \return Pair {bitmap (64 bools, row-major), total quantisation error}.
 */
[[nodiscard]] std::pair<std::vector<bool>, double>
quantise_block(Magick::Image &img, unsigned x_, unsigned y_,
               int cidx0, int cidx1)
{
  const auto col0 = palette_color(cidx0);
  const auto col1 = palette_color(cidx1);
  std::vector<bool> bitmap;
  bitmap.reserve(BLK * BLK);
  double total = 0.0;

  for (unsigned dy = 0; dy < BLK; ++dy)
    for (unsigned dx = 0; dx < BLK; ++dx) {
      const Magick::ColorRGB col(img.pixelColor(x_ + dx, y_ + dy));
      const bool use1 = col_dist(col, col1) < col_dist(col, col0);
      bitmap.push_back(use1);
      total += use1 ? col_dist(col, col1) : col_dist(col, col0);
      img.pixelColor(x_ + dx, y_ + dy, use1 ? col1 : col0);
    }
  return { std::move(bitmap), total };
}

// ── main quantisation pass ────────────────────────────────────────────────────

/**
 * \brief Optimal block-wise quantisation by exhaustive two-colour search.
 *
 * For each 8×8 block all C(16,2) = 120 colour pairs are evaluated via
 * \c block_error and the pair with the lowest total error is chosen.
 * The image is modified in-place; CharBlock descriptors are returned.
 *
 * \param img  Image to process in-place (must be ≥320×200, TrueColorType).
 * \return List of CharBlocks in left-to-right, top-to-bottom order.
 */
[[nodiscard]] std::list<CharBlock> handle_block_wise(Magick::Image &img) {
  std::list<CharBlock> blocks;

  for (unsigned y = 0; y < img.rows(); y += BLK) {
    for (unsigned x = 0; x < img.columns(); x += BLK) {

      /* Exhaustive search over all C(NCOLORS,2) unordered colour pairs */
      double best_err = std::numeric_limits<double>::infinity();
      int best_i = 0, best_j = 1;

      for (int i : std::views::iota(0, NCOLORS))
        for (int j : std::views::iota(i + 1, NCOLORS))
          if (const double e = block_error(img, x, y, i, j); e < best_err)
            std::tie(best_i, best_j, best_err) = std::tuple{i, j, e};

      auto [bitmap, err] = quantise_block(img, x, y, best_i, best_j);
      blocks.push_back({ best_i, best_j, std::move(bitmap) });
    }
  }
  return blocks;
}

// ── diagnostic helper ─────────────────────────────────────────────────────────

/**
 * \brief Snap every pixel independently to its nearest C64 palette colour.
 *
 * Also prints the colour and per-palette distances of pixel (0,0) to stdout.
 * This is a diagnostic tool and is not called in the normal processing path.
 *
 * \param img     Image to process in-place.
 * \param x_      Left edge of the region.
 * \param y_      Top edge of the region.
 * \param width   Region width in pixels.
 * \param height  Region height in pixels.
 */
void handle_image(Magick::Image &img,
                  unsigned x_, unsigned y_,
                  unsigned width, unsigned height)
{
  img.type(Magick::TrueColorType);
  const Magick::ColorRGB origin(img.pixelColor(0, 0));
  std::cout << std::format("{} {} {}\n",
                           origin.red(), origin.green(), origin.blue());

  for (int i = 0; i < NCOLORS; ++i) {
    const auto   pc = palette_color(i);
    const double d  = col_dist(origin, pc);
    std::cout << std::format("{:02X} {:13.6e} {:13.6e} {:13.6e} {:13.6E}\n", i,
                             std::abs(origin.red()   - pc.red()),
                             std::abs(origin.green() - pc.green()),
                             std::abs(origin.blue()  - pc.blue()),
                             d);
  }

  for (unsigned dy = 0; dy < height; ++dy)
    for (unsigned dx = 0; dx < width; ++dx) {
      Magick::ColorRGB col(img.pixelColor(x_ + dx, y_ + dy));
      img.pixelColor(x_ + dx, y_ + dy, palette_color(nearest_color(col)));
    }
}

// ── entry point ───────────────────────────────────────────────────────────────

/**
 * \brief Program entry point.
 *
 * Parses CLI options, loads and crops the image, runs block quantisation,
 * writes the C64 binary output, and optionally writes ILBM/XPM files and
 * displays the image.
 *
 * \param argc  Argument count.
 * \param argv  Argument vector.
 * \return 0 on success, non-zero on error.
 */
int main(int argc, char **argv) {
  CLI::App app{"graphconv – convert an image to C64 hires bitmap format"};

  std::string input_file;
  bool write_ilbm  = false;
  bool write_xpm   = false;
  bool display_gfx = false;

  app.add_option("file", input_file, "Input image file to convert")
     ->required()
     ->check(CLI::ExistingFile);
  app.add_flag("--write-ilbm", write_ilbm,  "Also save the quantised image as ILBM");
  app.add_flag("--write-xpm",  write_xpm,   "Also save the quantised image as XPM");
  app.add_flag("--display",    display_gfx, "Display the image before and after conversion");

  CLI11_PARSE(app, argc, argv);

  Magick::Image img(input_file);
  img.crop(Magick::Geometry(IMG_W, IMG_H, 0, 0));

  if (img.columns() < IMG_W || img.rows() < IMG_H)
    throw std::invalid_argument(
      std::format("wrong picture size ({}x{})", img.columns(), img.rows()));

  /*
   * Switch to TrueColorType so that pixelColor() returns plain RGB values.
   * See https://www.imagemagick.org/Magick++/Pixels.html
   */
  img.type(Magick::TrueColorType);

  if (display_gfx) img.display();

  const std::list<CharBlock> blocks = handle_block_wise(img);

  if (write_ilbm) img.write(change_ending(input_file, "ilbm"));
  if (write_xpm)  img.write(change_ending(input_file, "xpm"));

  /* Always write the native C64 binary; open explicitly in binary mode */
  std::ofstream outfile(change_ending(input_file, "c64"), std::ios::binary);
  write_char_blocks(blocks, outfile);

  if (display_gfx) img.display();
  return 0;
}
