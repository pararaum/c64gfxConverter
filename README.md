# c64gfxConverter #

Tools to convert graphics in order to use them on the C64. Graphics
encompasses bitmap and PETSCII graphics.

## chargenconv ##

Convert a 128*128 pixel black and white graphic into a font.

## graphconv ##

Convert a 320*200 pixel hires bitmap into a C64 format.

## petscii80x50 ##

Convert a 80*50 pixel bw image into a petscii screen.

## petsciiconvert ##

Convert animation in the `*.c` format into binary data.

## spriteconv ##

Convert a spritesheet image into sprite data.

# Building #

The following libraries must be installed:

 * libmagick++
 * SDL2
 * SDL2 image library

On a Debian based system the following command will do:

    sudo apt-get install libmagick++-dev libsdl2-image-dev libsdl2-dev

If you forgot to clone the repository recursively then install and
update the git submodules with

    git submodule init
    git submodule update

Then issue "make".


# Usage #

## chargenconv ##

## graphconv ##

Call the program with an image file. Only the upper left 320 times 200
pixels are converted.

    graphconv <filename>

## petscii80x50 ##
## petsciiconvert ##
## spriteconv ##

# Links #

 * http://www.syntiac.com/tech_ga_c64.html
 * http://www.spritemate.com/

## Dithering algorithms ##

 * https://www.r-bloggers.com/r-is-a-cool-image-editor-2-dithering-algorithms/
 * http://www.tannerhelland.com/4660/dithering-eleven-algorithms-source-code/
 * http://cv.ulichney.com/papers/2006-hexagonal-blue-noise.pdf
 * http://ulichney.com/
 * https://www.imaging.org/site/PDFS/Papers/1999/RP-0-93/1786.pdf
