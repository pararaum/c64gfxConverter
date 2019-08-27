# c64gfxConverter

Tools to convert graphics in order to use them on the C64.

# Compilation

The following libraries must be installed:

 * libmagick++

On a Debian based system the following command will do:

    sudo apt-get install libmagick++-dev

Then issue "make".

# Usage

Call the program with an image file. Only the upper left 320 times 200
pixels are converted.

    graphconv <filename>

# Links

 * http://www.syntiac.com/tech_ga_c64.html
 * http://www.spritemate.com/

## Dithering algorithms ##

 * https://www.r-bloggers.com/r-is-a-cool-image-editor-2-dithering-algorithms/
 * http://www.tannerhelland.com/4660/dithering-eleven-algorithms-source-code/
 * http://cv.ulichney.com/papers/2006-hexagonal-blue-noise.pdf
 * http://ulichney.com/
 * https://www.imaging.org/site/PDFS/Papers/1999/RP-0-93/1786.pdf
