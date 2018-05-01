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
