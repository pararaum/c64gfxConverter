#include <cmath>
#include <Magick++.h>
#include <iostream>

#define DEFAULT_THRESHOLD 0.5

/*
 * Actually the convertion is currently to screen codes not to
 * PETSCII. TODO: This needs to be fixed! OR cmd-line switch!
 */

const int petscii_block_chars[16] = {
  /* 0000 */ ' ', /* Nothing set */
  /* 0001 */ 108, /* ▗ */
  /* 0010 */ 123, /* ▖ */
  /* 0011 */ 98,  /* ▄ */
  /* 0100 */ 124, /* upper right */
  /* 0101 */ 225, /* ▐ */
  /* 0110 */ 255, /* ▞ */
  /* 0111 */ 254, /* ▟ */
  /* 1000 */ 126, /* upper left ▘ */
  /* 1001 */ 127, /* ▚ */
  /* 1010 */ 97, /* ▌ */
  /* 1011 */ 252, /* ▙*/
  /* 1100 */ 226, /* ▀ */
  /* 1101 */ 251, /* ▜ */
  /* 1110 */ 236, /* ▛ */
  /* 1111 */ 224 /* full block █ */
};

void scan_image(const Magick::Image &img) {
  unsigned int row, column;
  int idx;
  
  
  for(row = 0; row < img.rows(); row += 2) {
    for(column = 0; column < img.columns(); column += 2) {
      Magick::ColorMono coltl(img.pixelColor(column, row));
      Magick::ColorMono coltr(img.pixelColor(column + 1, row));
      Magick::ColorMono colbl(img.pixelColor(column, row + 1));
      Magick::ColorMono colbr(img.pixelColor(column + 1, row + 1));
      /*
	Now convert:
	12
	34
	to a binary number 1234 which we use as an index in the screen code table.
       */
      idx = !coltl.mono() << 3 | !coltr.mono() << 2 | !colbl.mono() << 1 | !colbr.mono();
      std::cout.put(petscii_block_chars[idx]);
    }
    //std::cout << '\n';
  }
  
}

int main(int argc, char **argv) {
  if(argc < 2) {
    std::cerr << "Usage: petscii80x25 <file>\n";
    return 1;
  } else {
    Magick::Image img(argv[1]);
    if(img.columns() > 80  || img.rows() > 50) {
      std::cerr << "Resizing image from (" << img.columns() << '*' << img.rows() << ").\n";
      img.resize(Magick::Geometry(80, 50));
    }
    img.threshold(DEFAULT_THRESHOLD);
    //img.display();
    scan_image(img);
  }
  return 0;
}
