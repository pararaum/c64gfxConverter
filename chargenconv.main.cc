#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <format>
#include <CLI/CLI.hpp>
#include "change_ending.hh"
#include "chargenconv.hh"

int main(int argc, char **argv) {
  CLI::App app{std::format("chargenconv V{} – convert a font image to C64 character-generator data", SUITE_VERSION)};
  std::string input_file;
  bool display_gfx = false;
  bool save_img = false;
  std::optional<unsigned short> loadaddr; // Optional load address to prepend.

  app.add_option("file", input_file, "input image file to convert")
    ->required()
    ->check(CLI::ExistingFile);
  app.add_option("--load-addr", loadaddr, "Load address to use for output")
    ->check(CLI::Range(0,65535));
  app.add_flag("--display,-d", display_gfx, "Display the image");
  app.add_flag("--save-xpm", save_img, "Save a XPM image");
  CLI11_PARSE(app, argc, argv);

  Magick::Image img(input_file);
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
  if(display_gfx) {
    img.display(); // After display, image data is botched???
  }
  if(save_img) {
    //img.write(change_ending(input_file, "ilbm"));
    img.write(change_ending(input_file, "xpm"));
  }
  std::ofstream outfile(change_ending(input_file, "c64"));
  if(loadaddr) {
    std::cout << std::format("Writing load address ${:04x}.\n", loadaddr.value());
    outfile.put(static_cast<char>(loadaddr.value() & 0xFF));
    outfile.put(static_cast<char>((loadaddr.value() >> 8) & 0xFF));
  }
  std::copy(chargen.begin(), chargen.end(), std::ostream_iterator<char>(outfile));
  return 0;
}
