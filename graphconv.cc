/**
 * \file graphconv.cc
 * \brief Convert an image to a Commodore 64 hires bitmap format.
 *
 * Loads an image (via ImageMagick/Magick++), crops it to 320×200 pixels,
 * quantises every 8×8 pixel block to the two best-matching C64 palette
 * colours, and writes the result as a raw C64 bitmap file (.c64).
 * Optionally the quantised image can also be saved as ILBM and/or XPM.
 *
 * Two quantisation modes are available (selectable at runtime):
 * - Default: nearest-colour snap per pixel, block colour pair chosen by
 *   exhaustive error minimisation.
 * - Stucki: block-constrained Stucki error diffusion for smoother output.
 *
 * Multiple C64 palettes are available via --palette:
 * - grafx2   : Grafx2 default (original palette in this tool)
 * - pepto    : Phillip Timmermann's mathematically derived palette
 * - colodore : Paolo Paglianti's Colodore palette (perceptually optimised)
 * - vice     : VICE emulator default palette
 * - ccs64    : CCS64 emulator palette
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
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>
#include <Magick++.h>
#include <CLI/CLI.hpp>
#include "change_ending.hh"

// ── compile-time constants ────────────────────────────────────────────────────

inline constexpr unsigned IMG_W   = 320; ///< C64 hires bitmap width in pixels
inline constexpr unsigned IMG_H   = 200; ///< C64 hires bitmap height in pixels
inline constexpr unsigned BLK     = 8;   ///< Character block side length in pixels
inline constexpr int      NCOLORS = 16;  ///< Number of C64 palette entries

// ── C64 palette definitions ───────────────────────────────────────────────────

/**
 * \brief Symbolic names for the 16 C64 palette entries.
 *
 * The ordering matches the hardware colour register numbers 0–15.
 */
enum class C64Color : int {
    Black = 0, White, Red, Cyan, Purple, Green, Blue, Yellow,
    Orange, Brown, LightRed, DarkGrey, Grey, LightGreen, LightBlue, LightGrey
};

/// Type alias: a full 16-entry palette in normalised RGB (0.0–1.0).
using C64Palette = std::array<std::array<double, 3>, NCOLORS>;

/**
 * \brief Grafx2 palette (original palette shipped with this tool).
 *
 * Values taken from the Grafx2 palette definition file c64vic20.pal.
 */
constexpr C64Palette palette_grafx2 {{
    {{ 0.000000e+00, 0.000000e+00, 0.000000e+00 }}, ///<  0 Black
    {{ 1.000000e+00, 1.000000e+00, 1.000000e+00 }}, ///<  1 White
    {{ 4.078431e-01, 2.156863e-01, 1.686275e-01 }}, ///<  2 Red
    {{ 4.392157e-01, 6.431373e-01, 6.980392e-01 }}, ///<  3 Cyan
    {{ 4.352941e-01, 2.392157e-01, 5.254902e-01 }}, ///<  4 Purple
    {{ 3.450980e-01, 5.529412e-01, 2.627451e-01 }}, ///<  5 Green
    {{ 2.078431e-01, 1.568627e-01, 4.745098e-01 }}, ///<  6 Blue
    {{ 7.215686e-01, 7.803922e-01, 4.352941e-01 }}, ///<  7 Yellow
    {{ 4.352941e-01, 3.098039e-01, 1.450980e-01 }}, ///<  8 Orange
    {{ 2.627451e-01, 2.235294e-01, 0.000000e+00 }}, ///<  9 Brown
    {{ 6.039216e-01, 4.039216e-01, 3.490196e-01 }}, ///< 10 LightRed
    {{ 2.666667e-01, 2.666667e-01, 2.666667e-01 }}, ///< 11 DarkGrey
    {{ 4.235294e-01, 4.235294e-01, 4.235294e-01 }}, ///< 12 Grey
    {{ 6.039216e-01, 8.235294e-01, 5.176471e-01 }}, ///< 13 LightGreen
    {{ 4.235294e-01, 3.686275e-01, 7.098039e-01 }}, ///< 14 LightBlue
    {{ 5.843137e-01, 5.843137e-01, 5.843137e-01 }}, ///< 15 LightGrey
}};

/**
 * \brief Pepto palette by Phillip Timmermann.
 *
 * Mathematically derived from the C64's VIC-II colour generation circuit.
 * Reference: https://www.pepto.de/projects/colorvic/
 * RGB values (sRGB, 8-bit): normalised to [0,1] here.
 *
 *  0 Black      #000000    8 Orange     #8D3105
 *  1 White      #FFFFFF    9 Brown      #5B4100
 *  2 Red        #68372B   10 LightRed   #9A6759
 *  3 Cyan       #70A4B2   11 DarkGrey   #444444
 *  4 Purple     #6F3D86   12 Grey       #6C6C6C
 *  5 Green      #588D43   13 LightGreen #9AD284
 *  6 Blue       #352879   14 LightBlue  #6C5EB5
 *  7 Yellow     #B8C76F   15 LightGrey  #959595
 */
constexpr C64Palette palette_pepto {{
    {{ 0x00/255.0, 0x00/255.0, 0x00/255.0 }}, ///<  0 Black
    {{ 0xFF/255.0, 0xFF/255.0, 0xFF/255.0 }}, ///<  1 White
    {{ 0x68/255.0, 0x37/255.0, 0x2B/255.0 }}, ///<  2 Red
    {{ 0x70/255.0, 0xA4/255.0, 0xB2/255.0 }}, ///<  3 Cyan
    {{ 0x6F/255.0, 0x3D/255.0, 0x86/255.0 }}, ///<  4 Purple
    {{ 0x58/255.0, 0x8D/255.0, 0x43/255.0 }}, ///<  5 Green
    {{ 0x35/255.0, 0x28/255.0, 0x79/255.0 }}, ///<  6 Blue
    {{ 0xB8/255.0, 0xC7/255.0, 0x6F/255.0 }}, ///<  7 Yellow
    {{ 0x8D/255.0, 0x31/255.0, 0x05/255.0 }}, ///<  8 Orange
    {{ 0x5B/255.0, 0x41/255.0, 0x00/255.0 }}, ///<  9 Brown
    {{ 0x9A/255.0, 0x67/255.0, 0x59/255.0 }}, ///< 10 LightRed
    {{ 0x44/255.0, 0x44/255.0, 0x44/255.0 }}, ///< 11 DarkGrey
    {{ 0x6C/255.0, 0x6C/255.0, 0x6C/255.0 }}, ///< 12 Grey
    {{ 0x9A/255.0, 0xD2/255.0, 0x84/255.0 }}, ///< 13 LightGreen
    {{ 0x6C/255.0, 0x5E/255.0, 0xB5/255.0 }}, ///< 14 LightBlue
    {{ 0x95/255.0, 0x95/255.0, 0x95/255.0 }}, ///< 15 LightGrey
}};

/**
 * \brief Colodore palette by Paolo Paglianti.
 *
 * Perceptually optimised palette derived from extensive measurement.
 * Reference: https://www.colodore.com/
 * RGB values (sRGB, 8-bit): normalised to [0,1] here.
 *
 *  0 Black      #000000    8 Orange     #8E3620
 *  1 White      #FFFFFF    9 Brown      #604000
 *  2 Red        #6D3B37   10 LightRed   #9A6758
 *  3 Cyan       #6EB6CC   11 DarkGrey   #444444
 *  4 Purple     #703778   12 Grey       #6C6C6C
 *  5 Green      #56892E   13 LightGreen #96D862
 *  6 Blue       #2F2EA6   14 LightBlue  #6C5ACA
 *  7 Yellow     #BBBA6E   15 LightGrey  #959595
 */
constexpr C64Palette palette_colodore {{
    {{ 0x00/255.0, 0x00/255.0, 0x00/255.0 }}, ///<  0 Black
    {{ 0xFF/255.0, 0xFF/255.0, 0xFF/255.0 }}, ///<  1 White
    {{ 0x6D/255.0, 0x3B/255.0, 0x37/255.0 }}, ///<  2 Red
    {{ 0x6E/255.0, 0xB6/255.0, 0xCC/255.0 }}, ///<  3 Cyan
    {{ 0x70/255.0, 0x37/255.0, 0x78/255.0 }}, ///<  4 Purple
    {{ 0x56/255.0, 0x89/255.0, 0x2E/255.0 }}, ///<  5 Green
    {{ 0x2F/255.0, 0x2E/255.0, 0xA6/255.0 }}, ///<  6 Blue
    {{ 0xBB/255.0, 0xBA/255.0, 0x6E/255.0 }}, ///<  7 Yellow
    {{ 0x8E/255.0, 0x36/255.0, 0x20/255.0 }}, ///<  8 Orange
    {{ 0x60/255.0, 0x40/255.0, 0x00/255.0 }}, ///<  9 Brown
    {{ 0x9A/255.0, 0x67/255.0, 0x58/255.0 }}, ///< 10 LightRed
    {{ 0x44/255.0, 0x44/255.0, 0x44/255.0 }}, ///< 11 DarkGrey
    {{ 0x6C/255.0, 0x6C/255.0, 0x6C/255.0 }}, ///< 12 Grey
    {{ 0x96/255.0, 0xD8/255.0, 0x62/255.0 }}, ///< 13 LightGreen
    {{ 0x6C/255.0, 0x5A/255.0, 0xCA/255.0 }}, ///< 14 LightBlue
    {{ 0x95/255.0, 0x95/255.0, 0x95/255.0 }}, ///< 15 LightGrey
}};

/**
 * \brief VICE emulator default palette.
 *
 * The palette used by VICE 3.x as its built-in default (c64hq palette).
 * Reference: VICE source tree data/C64/vice.vpl
 * RGB values (sRGB, 8-bit): normalised to [0,1] here.
 *
 *  0 Black      #000000    8 Orange     #8D4616
 *  1 White      #FFFFFF    9 Brown      #654100
 *  2 Red        #8D2020   10 LightRed   #C06060
 *  3 Cyan       #4DB4C7   11 DarkGrey   #404040
 *  4 Purple     #813079   12 Grey       #707070
 *  5 Green      #4D9051   13 LightGreen #82D37E
 *  6 Blue       #2B2B8D   14 LightBlue  #6060C0
 *  7 Yellow     #BDBD6D   15 LightGrey  #A0A0A0
 */
constexpr C64Palette palette_vice {{
    {{ 0x00/255.0, 0x00/255.0, 0x00/255.0 }}, ///<  0 Black
    {{ 0xFF/255.0, 0xFF/255.0, 0xFF/255.0 }}, ///<  1 White
    {{ 0x8D/255.0, 0x20/255.0, 0x20/255.0 }}, ///<  2 Red
    {{ 0x4D/255.0, 0xB4/255.0, 0xC7/255.0 }}, ///<  3 Cyan
    {{ 0x81/255.0, 0x30/255.0, 0x79/255.0 }}, ///<  4 Purple
    {{ 0x4D/255.0, 0x90/255.0, 0x51/255.0 }}, ///<  5 Green
    {{ 0x2B/255.0, 0x2B/255.0, 0x8D/255.0 }}, ///<  6 Blue
    {{ 0xBD/255.0, 0xBD/255.0, 0x6D/255.0 }}, ///<  7 Yellow
    {{ 0x8D/255.0, 0x46/255.0, 0x16/255.0 }}, ///<  8 Orange
    {{ 0x65/255.0, 0x41/255.0, 0x00/255.0 }}, ///<  9 Brown
    {{ 0xC0/255.0, 0x60/255.0, 0x60/255.0 }}, ///< 10 LightRed
    {{ 0x40/255.0, 0x40/255.0, 0x40/255.0 }}, ///< 11 DarkGrey
    {{ 0x70/255.0, 0x70/255.0, 0x70/255.0 }}, ///< 12 Grey
    {{ 0x82/255.0, 0xD3/255.0, 0x7E/255.0 }}, ///< 13 LightGreen
    {{ 0x60/255.0, 0x60/255.0, 0xC0/255.0 }}, ///< 14 LightBlue
    {{ 0xA0/255.0, 0xA0/255.0, 0xA0/255.0 }}, ///< 15 LightGrey
}};

/**
 * \brief CCS64 emulator palette.
 *
 * The palette used by Per Håkan Sundell's CCS64 emulator.
 * Reference: https://ccs64.com/ / community measurements
 * RGB values (sRGB, 8-bit): normalised to [0,1] here.
 *
 *  0 Black      #101010    8 Orange     #993300
 *  1 White      #FFFFFF    9 Brown      #663300
 *  2 Red        #993322   10 LightRed   #CC6655
 *  3 Cyan       #55CCDD   11 DarkGrey   #444444
 *  4 Purple     #882277   12 Grey       #777777
 *  5 Green      #33AA44   13 LightGreen #66DD55
 *  6 Blue       #2233BB   14 LightBlue  #5566EE
 *  7 Yellow     #CCDD55   15 LightGrey  #AAAAAA
 */
constexpr C64Palette palette_ccs64 {{
    {{ 0x10/255.0, 0x10/255.0, 0x10/255.0 }}, ///<  0 Black
    {{ 0xFF/255.0, 0xFF/255.0, 0xFF/255.0 }}, ///<  1 White
    {{ 0x99/255.0, 0x33/255.0, 0x22/255.0 }}, ///<  2 Red
    {{ 0x55/255.0, 0xCC/255.0, 0xDD/255.0 }}, ///<  3 Cyan
    {{ 0x88/255.0, 0x22/255.0, 0x77/255.0 }}, ///<  4 Purple
    {{ 0x33/255.0, 0xAA/255.0, 0x44/255.0 }}, ///<  5 Green
    {{ 0x22/255.0, 0x33/255.0, 0xBB/255.0 }}, ///<  6 Blue
    {{ 0xCC/255.0, 0xDD/255.0, 0x55/255.0 }}, ///<  7 Yellow
    {{ 0x99/255.0, 0x33/255.0, 0x00/255.0 }}, ///<  8 Orange
    {{ 0x66/255.0, 0x33/255.0, 0x00/255.0 }}, ///<  9 Brown
    {{ 0xCC/255.0, 0x66/255.0, 0x55/255.0 }}, ///< 10 LightRed
    {{ 0x44/255.0, 0x44/255.0, 0x44/255.0 }}, ///< 11 DarkGrey
    {{ 0x77/255.0, 0x77/255.0, 0x77/255.0 }}, ///< 12 Grey
    {{ 0x66/255.0, 0xDD/255.0, 0x55/255.0 }}, ///< 13 LightGreen
    {{ 0x55/255.0, 0x66/255.0, 0xEE/255.0 }}, ///< 14 LightBlue
    {{ 0xAA/255.0, 0xAA/255.0, 0xAA/255.0 }}, ///< 15 LightGrey
}};

/// Registry: map palette name → pointer to palette data.
/// Extend this map to add further palettes without changing any other code.
const std::map<std::string, const C64Palette *> palette_registry {
    { "grafx2",   &palette_grafx2   },
    { "pepto",    &palette_pepto    },
    { "colodore", &palette_colodore },
    { "vice",     &palette_vice     },
    { "ccs64",    &palette_ccs64    },
};

/// The active palette, set by main() from --palette; defaults to grafx2.
const C64Palette *active_palette = &palette_grafx2;

// ── data types ────────────────────────────────────────────────────────────────

/**
 * \brief One 8×8 character block with its two palette colours.
 *
 * \c idx1 and \c idx2 index into the active palette (0–15).
 * \c data holds 64 per-pixel colour decisions in row-major order:
 * \c false → colour \c idx1, \c true → colour \c idx2.
 */
struct CharBlock {
    int idx1, idx2;      ///< Palette indices for the bit-0 and bit-1 colours
    std::vector<bool> data; ///< Per-pixel colour assignment (exactly 64 elements)
};

// ── colour helpers ────────────────────────────────────────────────────────────

/// Construct a Magick++ colour for palette index \p idx using the active palette.
[[nodiscard]] inline Magick::ColorRGB palette_color(int idx) noexcept {
    const auto &c = (*active_palette)[idx];
    return { c[0], c[1], c[2] };
}

/**
 * \brief Euclidean distance between two RGB colours in the [0,1]³ cube.
 */
[[nodiscard]] double col_dist(const Magick::ColorRGB &a,
                               const Magick::ColorRGB &b) noexcept
{
    return std::sqrt(std::pow(a.red()   - b.red(),   2)
                   + std::pow(a.green() - b.green(), 2)
                   + std::pow(a.blue()  - b.blue(),  2));
}

/**
 * \brief Index of the active palette entry closest to \p col.
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
 *  - 2-byte little-endian load address
 *  - Bitmap section: 8 bytes per block, one bit per pixel,
 *    left-to-right / top-to-bottom (320×200÷8 = 8 000 bytes)
 *  - Colour attribute section: one byte per block,
 *    high nybble = idx1, low nybble = idx2 (40×25 = 1 000 bytes)
 */
void write_char_blocks(const std::list<CharBlock> &blocks,
                       std::ostream &out,
                       unsigned short addr = 0x2000)
{
    std::cerr << std::format("Writing {} blocks\n", blocks.size());

    out << static_cast<char>(addr & 0xFF) << static_cast<char>(addr >> 8);

    for (const auto &blk : blocks) {
        assert(blk.data.size() == BLK * BLK);
        for (auto row : blk.data | std::views::chunk(BLK)) {
            uint8_t byte = 0;
            for (bool bit : row)
                byte = static_cast<uint8_t>((byte << 1) | (bit ? 1u : 0u));
            out << static_cast<char>(byte);
        }
    }

    for (const auto &blk : blocks)
        out << static_cast<char>((blk.idx1 << 4) | blk.idx2);

    constexpr unsigned gfx_bytes = IMG_W * IMG_H / BLK;
    std::cerr << std::format("Gfx: ${:04X}-${:04X}\n",
                             addr, addr + gfx_bytes - 1);
    std::cerr << std::format("Col: ${:04X}-${:04X}\n",
                             addr + gfx_bytes, addr + gfx_bytes + 40u*25u);
}

// ── block quantisation helpers ────────────────────────────────────────────────

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

// ── nearest-colour quantisation pass ─────────────────────────────────────────

[[nodiscard]] std::list<CharBlock> handle_block_wise(Magick::Image &img) {
    std::list<CharBlock> blocks;
    for (unsigned y = 0; y < img.rows(); y += BLK) {
        for (unsigned x = 0; x < img.columns(); x += BLK) {
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

// ── Stucki dithering pass ─────────────────────────────────────────────────────

[[nodiscard]] std::list<CharBlock> handle_block_wise_stucki(Magick::Image &img)
{
    const unsigned W = img.columns();
    const unsigned H = img.rows();

    using KernelEntry = std::tuple<int, int, int>;
    constexpr std::array<KernelEntry, 12> stucki_kernel {{
        {  1, 0, 8 }, {  2, 0, 4 },
        { -2, 1, 2 }, { -1, 1, 4 }, { 0, 1, 8 }, { 1, 1, 4 }, { 2, 1, 2 },
        { -2, 2, 1 }, { -1, 2, 2 }, { 0, 2, 4 }, { 1, 2, 2 }, { 2, 2, 1 },
    }};
    constexpr double STUCKI_DIV = 42.0;

    std::vector<std::array<double, 3>> err(W * H, {0.0, 0.0, 0.0});

    const unsigned BW = W / BLK;
    const unsigned BH = H / BLK;
    std::vector<std::pair<int,int>> block_colors(BW * BH);
    for (unsigned by = 0; by < BH; ++by)
        for (unsigned bx = 0; bx < BW; ++bx) {
            double best_err = std::numeric_limits<double>::infinity();
            int best_i = 0, best_j = 1;
            for (int i : std::views::iota(0, NCOLORS))
                for (int j : std::views::iota(i + 1, NCOLORS))
                    if (const double e = block_error(img, bx*BLK, by*BLK, i, j); e < best_err)
                        std::tie(best_i, best_j, best_err) = std::tuple{i, j, e};
            block_colors[by * BW + bx] = { best_i, best_j };
        }

    std::vector<std::vector<bool>> bitmaps(BW * BH,
                                           std::vector<bool>(BLK * BLK, false));

    for (unsigned y = 0; y < H; ++y) {
        for (unsigned x = 0; x < W; ++x) {
            const unsigned bx = x / BLK;
            const unsigned by = y / BLK;
            const auto [cidx0, cidx1] = block_colors[by * BW + bx];
            const auto pal0 = palette_color(cidx0);
            const auto pal1 = palette_color(cidx1);

            const Magick::ColorRGB orig(img.pixelColor(x, y));
            const auto &e = err[y * W + x];
            const Magick::ColorRGB corrected(
                std::clamp(orig.red()   + e[0], 0.0, 1.0),
                std::clamp(orig.green() + e[1], 0.0, 1.0),
                std::clamp(orig.blue()  + e[2], 0.0, 1.0)
            );

            const bool use1 = col_dist(corrected, pal1) < col_dist(corrected, pal0);
            const Magick::ColorRGB &chosen = use1 ? pal1 : pal0;

            bitmaps[by * BW + bx][(y % BLK) * BLK + (x % BLK)] = use1;
            img.pixelColor(x, y, chosen);

            const std::array<double, 3> qerr {
                corrected.red()   - chosen.red(),
                corrected.green() - chosen.green(),
                corrected.blue()  - chosen.blue(),
            };

            for (const auto &[dx, dy, w] : stucki_kernel) {
                const int nx = static_cast<int>(x) + dx;
                const int ny = static_cast<int>(y) + dy;
                if (nx < 0 || nx >= static_cast<int>(W) ||
                    ny < 0 || ny >= static_cast<int>(H))
                    continue;
                const double weight = static_cast<double>(w) / STUCKI_DIV;
                auto &ne = err[ny * W + nx];
                ne[0] += qerr[0] * weight;
                ne[1] += qerr[1] * weight;
                ne[2] += qerr[2] * weight;
            }
        }
    }

    std::list<CharBlock> blocks;
    for (unsigned by = 0; by < BH; ++by)
        for (unsigned bx = 0; bx < BW; ++bx) {
            const auto [i, j] = block_colors[by * BW + bx];
            blocks.push_back({ i, j, std::move(bitmaps[by * BW + bx]) });
        }
    return blocks;
}

// ── diagnostic helper ─────────────────────────────────────────────────────────

void handle_image(Magick::Image &img,
                  unsigned x_, unsigned y_,
                  unsigned width, unsigned height)
{
    img.type(Magick::TrueColorType);
    const Magick::ColorRGB origin(img.pixelColor(0, 0));
    std::cout << std::format("{} {} {}\n",
                             origin.red(), origin.green(), origin.blue());
    for (int i = 0; i < NCOLORS; ++i) {
        const auto pc = palette_color(i);
        const double d = col_dist(origin, pc);
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
 * Parses CLI options, loads and crops the image, runs the chosen quantisation
 * pass, writes the C64 binary output, and optionally writes ILBM/XPM files
 * and displays the image.
 */
int main(int argc, char **argv) {
    CLI::App app{"graphconv – convert an image to C64 hires bitmap format"};

    std::string input_file;
    bool write_ilbm   = false;
    bool write_xpm    = false;
    bool display_gfx  = false;
    bool use_stucki   = false;
    std::string palette_name = "grafx2";

    app.add_option("file", input_file, "Input image file to convert")
        ->required()
        ->check(CLI::ExistingFile);

    app.add_flag("--write-ilbm", write_ilbm,
                 "Also save the quantised image as ILBM");
    app.add_flag("--write-xpm",  write_xpm,
                 "Also save the quantised image as XPM");
    app.add_flag("--display", display_gfx,
                 "Display the image before and after conversion");
    app.add_flag("--stucki", use_stucki,
                 "Use block-constrained Stucki error diffusion instead of "
                 "nearest-colour quantisation (smoother output, slower)");

    // Build a human-readable list of palette names for the help text.
    std::string palette_list;
    for (const auto &[name, _] : palette_registry) {
        if (!palette_list.empty()) palette_list += ", ";
        palette_list += name;
    }
    app.add_option("--palette", palette_name,
                   std::format("C64 colour palette to use (default: grafx2).\n"
                               "Available palettes: {}", palette_list))
        ->check([](const std::string &val) -> std::string {
            if (palette_registry.count(val) == 0)
                return std::format("unknown palette '{}'. "
                                   "Run with --help for a list of valid palettes.", val);
            return {};
        });

    CLI11_PARSE(app, argc, argv);

    // Activate the selected palette (global pointer used by palette_color()).
    active_palette = palette_registry.at(palette_name);
    std::cerr << std::format("Using palette: {}\n", palette_name);

    Magick::Image img(input_file);
    img.crop(Magick::Geometry(IMG_W, IMG_H, 0, 0));

    if (img.columns() < IMG_W || img.rows() < IMG_H)
        throw std::invalid_argument(
            std::format("wrong picture size ({}x{})", img.columns(), img.rows()));

    img.type(Magick::TrueColorType);

    if (display_gfx) img.display();

    const std::list<CharBlock> blocks = use_stucki
        ? handle_block_wise_stucki(img)
        : handle_block_wise(img);

    if (write_ilbm) img.write(change_ending(input_file, "ilbm"));
    if (write_xpm)  img.write(change_ending(input_file, "xpm"));

    std::ofstream outfile(change_ending(input_file, "c64"), std::ios::binary);
    write_char_blocks(blocks, outfile);

    if (display_gfx) img.display();

    return 0;
}
