# c64gfxConverter #

Tools to convert graphics in order to use them on the C64. Graphics
encompasses bitmap and PETSCII graphics.

## chargenconv ##

Convert a 128*128 pixel black and white graphic into a font.

## graphconv ##

Convert a 320*200 pixel hires bitmap into a C64 format.

## petscii80x50 ##

Convert a 80*50 pixel black and white image into a screencode/petscii screen.

## petsciiconvert ##

Convert animation in the `*.c` format into binary data.

## spriteconv ##

Convert a spritesheet image into sprite data.

# Building #

The following libraries must be installed:

 * libmagick++
 * SDL2
 * SDL2 image library
 * CLI11 library

On a Debian based system the following command will do:

    sudo apt-get install libmagick++-dev libsdl2-image-dev libsdl2-dev libcli11-dev

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

There are other command-line options:

```
graphconv – convert an image to C64 hires bitmap format
Usage: ./graphconv [OPTIONS] file

Positionals:
  file TEXT:FILE REQUIRED     Input image file to convert

Options:
  -h,--help                   Print this help message and exit
  --write-ilbm                Also save the quantised image as ILBM
  --write-xpm                 Also save the quantised image as XPM
  --display                   Display the image before and after conversion
```

## petscii80x50 ##

```
petscii80x50 – convert an image to C64 screen-code block characters
Usage: ./petscii80x50 [OPTIONS] file

Positionals:
  file TEXT:FILE REQUIRED     Input image file to convert

Options:
  -h,--help                   Print this help message and exit
  -t,--threshold FLOAT:FLOAT in [0 - 1]
                              Luminance threshold for 1-bit conversion (0.0–1.0, default 0.5)
  -d,--display                Display the thresholded image on screen before converting
  --load-address              prepend a load address to the output
```

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
