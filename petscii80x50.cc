/**
 * \file petscii80x50.cc
 * \brief Convert an image to a stream of C64 screen-code block characters.
 *
 * Loads an image, optionally resizes it to fit within 80×50 pixels, applies
 * a configurable luminance threshold to produce a 1-bit image, then maps
 * every 2×2 pixel quad to the closest C64 block character (screen code).
 *
 * The output is a raw byte stream of screen codes, one character per 2×2
 * pixel region, suitable for POKE-ing directly into C64 screen RAM.
 *
 * \note The mapping currently produces \b screen codes, not PETSCII codes.
 *       These are numerically different for many characters.  A future
 *       \c --petscii flag should add a translation pass before output.
 *
 * Build dependencies: Magick++, CLI11
 * Requires: C++23 (-std=c++23)
 */

#include <array>
#include <format>
#include <iostream>
#include <ostream>

#include <Magick++.h>
#include <CLI/CLI.hpp>

// ── constants ─────────────────────────────────────────────────────────────────

/// Maximum image width accepted without resizing (one C64 screen column = 2 px).
inline constexpr unsigned MAX_W = 80;
/// Maximum image height accepted without resizing (one C64 screen row = 2 px).
inline constexpr unsigned MAX_H = 50;

/// Default luminance threshold for the 1-bit conversion (0.0–1.0).
inline constexpr double DEFAULT_THRESHOLD = 0.5;

// ── screen-code lookup table ──────────────────────────────────────────────────

/**
 * \brief C64 screen codes for the 16 possible 2×2 pixel block patterns.
 *
 * Each 2×2 quad is mapped to a 4-bit index by treating the four pixels as
 * binary digits arranged as:
 *
 * ```
 *   bit 3 (MSB)  bit 2
 *   bit 1        bit 0 (LSB)
 * ```
 *
 * A pixel that is \e on (foreground) contributes a 1 bit.
 * The resulting index selects the screen code of the C64 block character
 * whose filled quadrants match the lit pixels.
 *
 * \note These are \b screen codes (the raw values written to screen RAM),
 *       not PETSCII character codes.  See the file-level note.
 */
constexpr std::array<unsigned char, 16> screen_code_blocks {{
  /* 0b0000 */  32, ///< ' '  — blank
  /* 0b0001 */ 108, ///< ▗   — bottom-right
  /* 0b0010 */ 123, ///< ▖   — bottom-left
  /* 0b0011 */  98, ///< ▄   — bottom half
  /* 0b0100 */ 124, ///< ▝   — top-right
  /* 0b0101 */ 225, ///< ▐   — right half
  /* 0b0110 */ 255, ///< ▞   — diagonal (top-right + bottom-left)
  /* 0b0111 */ 254, ///< ▟   — all except top-left
  /* 0b1000 */ 126, ///< ▘   — top-left
  /* 0b1001 */ 127, ///< ▚   — diagonal (top-left + bottom-right)
  /* 0b1010 */  97, ///< ▌   — left half
  /* 0b1011 */ 252, ///< ▙   — all except top-right
  /* 0b1100 */ 226, ///< ▀   — top half
  /* 0b1101 */ 251, ///< ▜   — all except bottom-left
  /* 0b1110 */ 236, ///< ▛   — all except bottom-right
  /* 0b1111 */ 224, ///< █   — full block
}};

// ── image scanning ────────────────────────────────────────────────────────────

/**
 * \brief Convert a thresholded image to C64 screen-code block characters.
 *
 * Iterates over the image in 2×2 pixel steps.  For each quad the four pixel
 * luminances are read as mono (1-bit) values; the four bits are packed into a
 * nibble (top-left = MSB, bottom-right = LSB) and used as an index into
 * \c screen_code_blocks.  The resulting screen code byte is written to \p out.
 *
 * The image must already have been thresholded to a 1-bit (mono) state.
 * Its dimensions must be even in both axes; any trailing odd row or column
 * is silently ignored.
 *
 * \param img  Source image (read-only, must be mono/thresholded).
 * \param out  Destination byte stream for the screen code output.
 */
void scan_image(const Magick::Image &img, std::ostream &out) {
  for (unsigned row = 0; row + 1 < img.rows(); row += 2) {
    for (unsigned col = 0; col + 1 < img.columns(); col += 2) {
      /*
       * Sample the four pixels of the 2×2 quad:
       *   tl tr
       *   bl br
       *
       * ColorMono::mono() returns true for white (background) and false for
       * black (foreground), so we negate to treat dark pixels as "set" bits.
       */
      const bool tl = !Magick::ColorMono(img.pixelColor(col,     row    )).mono();
      const bool tr = !Magick::ColorMono(img.pixelColor(col + 1, row    )).mono();
      const bool bl = !Magick::ColorMono(img.pixelColor(col,     row + 1)).mono();
      const bool br = !Magick::ColorMono(img.pixelColor(col + 1, row + 1)).mono();

      const unsigned idx = (tl << 3) | (tr << 2) | (bl << 1) | br;
      out.put(static_cast<char>(screen_code_blocks[idx]));
    }
  }
}

// ── entry point ───────────────────────────────────────────────────────────────

/**
 * \brief Program entry point.
 *
 * Parses CLI options, loads the image, resizes if necessary, applies the
 * luminance threshold, and writes the screen-code byte stream to stdout.
 *
 * \param argc  Argument count.
 * \param argv  Argument vector.
 * \return 0 on success, non-zero on error.
 */
int main(int argc, char **argv) {
  CLI::App app{"petscii80x50 – convert an image to C64 screen-code block characters"};

  std::string input_file;
  double threshold   = DEFAULT_THRESHOLD;
  bool   display_gfx = false;
  std::optional<unsigned short> load_address;

  app.add_option("file", input_file, "Input image file to convert")
     ->required()
     ->check(CLI::ExistingFile);
  app.add_option("--threshold,-t", threshold,
                 std::format("Luminance threshold for 1-bit conversion "
                             "(0.0–1.0, default {:.1f})", DEFAULT_THRESHOLD))
     ->check(CLI::Range(0.0, 1.0));
  app.add_flag("--display,-d", display_gfx,
               "Display the thresholded image on screen before converting");
  app.add_flag("--load-address", load_address, "prepend a load address to the output");

  CLI11_PARSE(app, argc, argv);

  Magick::Image img(input_file);

  /* Resize down if the image is larger than the maximum accepted dimensions */
  if (img.columns() > MAX_W || img.rows() > MAX_H) {
    std::cerr << std::format("Resizing image from {}x{} to fit within {}x{}.\n",
                             img.columns(), img.rows(), MAX_W, MAX_H);
    img.resize(Magick::Geometry(MAX_W, MAX_H));
  }

  img.threshold(threshold);

  if (display_gfx) img.display();

  if(load_address) {
    unsigned short loadaddress16bit = load_address.value();
    std::cerr << std::format("Prepending a load address of ${:04X}.\n", loadaddress16bit);
    std::cout << static_cast<char>(loadaddress16bit & 0xFF) << static_cast<char>(loadaddress16bit >> 8);
  }
  scan_image(img, std::cout);

  return 0;
}
