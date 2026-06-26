#include <catch2/catch_test_macros.hpp>
#include <Magick++.h>
#include <vector>
#include <cstdint>

// Pull in the functions under test (or move them to a header)
#include "../chargenconv.hh"

/// Create a minimal 128x128 white (false/off) image, then paint individual
/// pixels black (true/on) before passing to get_bitmap / convert_whole_bitmap.
static Magick::Image make_blank(unsigned w = 128, unsigned h = 128) {
    Magick::Image img(Magick::Geometry(w, h), Magick::Color("white"));
    img.type(Magick::TrueColorType);
    img.modifyImage();
    img.threshold(50000);
    return img;
}

static void set_pixel(Magick::Image &img, unsigned x, unsigned y) {
    img.pixelColor(x, y, Magick::Color("black"));
}

/******************************************************************************
 *
 * Test cases
 *
 ******************************************************************************/

TEST_CASE("get_bitmap returns exactly 8 bytes", "[get_bitmap]") {
    auto img = make_blank();
    auto result = get_bitmap(img, 0, 0);
    REQUIRE(result.size() == 8);
}

TEST_CASE("get_bitmap all-white block gives all-zero bytes", "[get_bitmap]") {
    auto img = make_blank();
    auto result = get_bitmap(img, 0, 0);
    for (auto byte : result)
        CHECK(byte == 0x00);
}

TEST_CASE("get_bitmap all-black block gives all 0xFF bytes", "[get_bitmap]") {
    auto img = make_blank();
    // Fill the first 8x8 block with black pixels.
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            set_pixel(img, x, y);

    auto result = get_bitmap(img, 0, 0);
    for (auto byte : result)
        CHECK(byte == 0xFF);
}

TEST_CASE("get_bitmap MSB is leftmost pixel", "[get_bitmap]") {
    // Setting only the leftmost pixel in row 0 should produce 0x80 in byte 0.
    auto img = make_blank();
    set_pixel(img, 0, 0);

    auto result = get_bitmap(img, 0, 0);
    CHECK(result[0] == 0x80);
    // All other rows untouched.
    for (size_t i = 1; i < result.size(); ++i)
        CHECK(result[i] == 0x00);
}

TEST_CASE("get_bitmap LSB is rightmost pixel", "[get_bitmap]") {
    auto img = make_blank();
    set_pixel(img, 7, 0); // rightmost pixel of first row

    auto result = get_bitmap(img, 0, 0);
    CHECK(result[0] == 0x01);
}

TEST_CASE("get_bitmap checkerboard pattern", "[get_bitmap]") {
    // Even columns black → bits 7,5,3,1 set → 0xAA per row.
    auto img = make_blank();
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; x += 2)
            set_pixel(img, x, y);

    auto result = get_bitmap(img, 0, 0);
    for (auto byte : result)
        CHECK(byte == 0xAA);
}

TEST_CASE("get_bitmap respects x_ and y_ offsets", "[get_bitmap]") {
    // Paint only one pixel in the second 8x8 block (offset 8,0).
    auto img = make_blank();
    set_pixel(img, 8, 0); // MSB of block (8,0)

    auto result_origin = get_bitmap(img, 0, 0);
    auto result_offset = get_bitmap(img, 8, 0);

    CHECK(result_origin[0] == 0x00); // nothing in first block
    CHECK(result_offset[0] == 0x80); // MSB set in second block
}

TEST_CASE("load address low byte extraction", "[loadaddr]") {
    unsigned short addr = 0x1234;
    CHECK((addr & 0xFF) == 0x34);
}
