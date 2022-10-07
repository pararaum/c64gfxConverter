/*! \file Simple program to convert an image to a C64 sprite.
 *
 * Every sprite has the 63 bytes of sprite data and the last byte
 * contains metadata.
 *
 * see https://csdb.dk/forums/?roomid=7&topicid=125812
 */

#include <getopt.h>
#include <iostream>
#include <map>
#include <cassert>
#include <boost/format.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "spriteconv_cli.h"

bool display_image(SDL_Surface *surface, SDL_Renderer *renderer) {
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);

  if(!texture) {
    std::cerr << "Error creating texture: " << SDL_GetError() << std::endl;
    return false;
  } else {
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_DestroyTexture(texture);
    SDL_RenderPresent(renderer);
  }
  return true;
}

struct SpriteInterface {
  virtual std::ostream &write_asm(std::ostream &out) const = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;
};

struct SpriteData : public SpriteInterface {
  int pixel[24][21];

  std::ostream &write_asm(std::ostream &out) const {
    for(int iy = 0; iy < 21; ++iy) {
      out << "\t.byte";
      for(int ix = 0; ix < 24; ix += 8) {
	unsigned int value = 0;
	for(int bit = 0; bit < 8; ++bit) {
	  if(pixel[ix + bit][iy] != 0) {
	    value |= 1 << (7 - bit);
	  }
	}
	out << boost::format(" $%02X%c") % value % (ix < 16 ? ',' : ' ');
      }
      out << '\n';
    }
    out << "\t.byte $00";
    return out;
  }
  virtual int width() const { return 24; }
  virtual int height() const { return 21; }
};

/*! \brief Multicoloured Sprite Data
 *
 * The information is stored in the C64 format. The colour information
 * is [http://codebase64.org/doku.php?id=base:spriteintro]:
 *
 *  - 00: transparent
 *  - 01: $d025
 *  - 11: $d026
 *  - 10: individual ($d027-$d02f)
 */
struct MultiSpriteData : public SpriteInterface {
  int pixel[12][21];

  std::ostream &write_asm(std::ostream &out) const {
    for(int iy = 0; iy < 21; ++iy) {
      out << "\t.byte";
      for(int ix = 0; ix < 12; ix += 4) {
	unsigned int value = 0;
	for(int bit = 0; bit < 8; bit += 2) {
	  value |= (pixel[ix + bit / 2][iy] & 3) << (6 - bit);
	}
	out << boost::format(" $%02X%c") % value % (ix < 8 ? ',' : ' ');
      }
      out << '\n';
    }
    out << "\t.byte $80";
    return out;
  }

  virtual int width() const { return 12; }
  virtual int height() const { return 21; }
};


int get_color(SDL_Surface *surface, int x, int y) {
  assert(x >= 0);
  assert(y >= 0);
  Uint8 *pixelline = static_cast<Uint8 *>(surface->pixels);
  pixelline += y * surface->pitch;
  int pixel = pixelline[x];
  return pixel;
}

/*! \brief convert image data to black and white sprite.
 *
 * The surface must be an 8 bit palette image!
 * 
 * \param surface Surface to get the data from
 * \param xpos X-Position of sprite in surface
 * \param ypos Y-Position of sprite in surface
 * \param transparent Colour index of transparent colour
 */
SpriteData convert_bw_sprite(SDL_Surface *surface, int xpos, int ypos, int transparent) {
  int pixel;
  SpriteData sprite;
  
  for(int row = 0; row < 21; ++row) {
    for(int column = 0; column < 24; ++column) {
      pixel = get_color(surface, xpos + column, ypos + row);
      assert(pixel >= 0);
      sprite.pixel[column][row] = pixel != transparent;
    }
  }
  return sprite;
}


/*! \brief convert image data to a multicolour sprite.
 *
 * The surface must be an 8 bit palette image!
 * 
 * \param surface Surface to get the data from
 * \param xpos X-Position of sprite in surface
 * \param ypos Y-Position of sprite in surface
 * \param transparent Colour index of transparent colour
 * \param m1 Multicolour one
 * \param m2 Multicolour two
 */
MultiSpriteData convert_multi_sprite(SDL_Surface *surface, int xpos, int ypos, int transparent, int m1, int m2) {
  int pixel;
  int pattern;
  MultiSpriteData sprite;
  
  for(int row = 0; row < 21; ++row) {
    for(int column = 0; column < 12; ++column) {
      pixel = get_color(surface, xpos + column, ypos + row);
      assert(pixel >= 0);
      if(pixel == transparent) {
	pattern = 0b00000000;
      } else if(pixel == m1) {
	pattern = 0b00000001;
      } else if(pixel == m2) {
	pattern = 0b00000011;
      } else {
	pattern = 0b00000010;
      }
      //printf("%4d %4d %4d %02X\n", column, row, pixel, pattern);
      sprite.pixel[column][row] = pattern;
    }
  }
  return sprite;
}


MultiSpriteData convert_sprite(SDL_Surface *surface, int x, int y, int border) {
  MultiSpriteData sprite;
  std::map<int,int> pixelvalues;
  int maxpixelval = 0;
  
  for(int iy = 0; iy < 21; ++iy) {
    Uint8 *pixelline = static_cast<Uint8 *>(surface->pixels);
    pixelline += (y + iy + border) * surface->pitch;
    for(int ix = 0; ix < 12; ++ix) {
      int pixel = pixelline[border + x + ix];
      auto found = pixelvalues.find(pixel);
      if(found == pixelvalues.end()) {
	sprite.pixel[ix][iy] = maxpixelval;
	pixelvalues[pixel] = maxpixelval++;
      } else {
	sprite.pixel[ix][iy] = found->second;
      }
    }
  }
  if(pixelvalues.size() > 4) {
    throw std::logic_error("too many pixel values");
  }
  return sprite;
}

void extract_sprite_data(SDL_Surface *surface, const gengetopt_args_info *args) {
  int x, y;
  auto rowheight = args->rowheight_arg;
  auto columnwidth = args->columnwidth_arg;

  SDL_LockSurface(surface);
  if(surface->format->BitsPerPixel != 8) {
    std::cerr << "Unknown pixel format!\n";
  } else {
    for(y = 0;
	(y < args->spritecolumns_arg)
	  || ((args->spritecolumns_arg < 0)
	      && (y * rowheight + args->y_position_arg < surface->h));
	y++) {
      for(x = 0;
	  (x < args->spriterows_arg)
	    || ((args->spriterows_arg < 0)
		&& (x * columnwidth + args->x_position_arg < surface->w));
	  x++) {
	auto x_position = x * columnwidth + args->x_position_arg;
	auto y_position = y * columnwidth + args->y_position_arg;
	if(args->labelname_given) {
	  std::cout << args->labelname_arg << x << y << ":\n";
	}
	if(args->multi_mode_counter == 0) { //No multicolour sprite
	  auto sprite = convert_bw_sprite(surface, x_position, y_position, args->transparent_arg);
	  sprite.write_asm(std::cout) << std::endl;
	} else { //Multicolour
	  MultiSpriteData sprite;
	  if(args->autocol_flag) {
	    sprite = convert_sprite(surface, x_position, y_position, 0);
	  } else {
	    sprite = convert_multi_sprite(surface, x_position, y_position, args->transparent_arg, args->multi1_arg, args->multi2_arg);
	  }
	  sprite.write_asm(std::cout) << std::endl;
	}
      } // Finish column.
    } // Finish rows.
  }
  SDL_UnlockSurface(surface);
}


int main(int argc, char **argv) {
  //SDL_Window *window;
  //SDL_Renderer *renderer;
  SDL_Surface *surface;
  gengetopt_args_info args_info;
  int ret = -1;
  const char *fname;

  auto cli = cmdline_parser(argc, argv, &args_info);
  if(cli != 0) {
    std::cerr << "Error while parsind command line!\n";
    return -1;
  }
  
  if(args_info.inputs_num != 1) {
    std::cerr << "Usage: spriteconv [-h | OPTIONS] <imagename>\n";
    ret = 1;
  } else if(SDL_Init(0/*SDL_INIT_VIDEO*/) == -1) {
    std::cerr << "SDL_Init() failed: " << SDL_GetError() << std::endl;
    ret = 2;
  } else {
    // if(SDL_CreateWindowAndRenderer(320, 200, 0, &window, &renderer) < 0) {
    //   std::cerr << "SDL_CreateWindowAndRenderer() failed: " << SDL_GetError() << std::endl;
    //   ret = 3;
    // } 
    fname = args_info.inputs[0];
    surface = IMG_Load(fname);
    if(surface) {
      // SDL_SetWindowTitle(window, fname);
      // SDL_SetWindowSize(window, surface->w, surface->h);
      // SDL_ShowWindow(window);
      // display_image(surface, renderer);
      // SDL_RenderPresent(renderer);
      extract_sprite_data(surface, &args_info);
      SDL_FreeSurface(surface);
    } else {
      std::cerr << "Error! Can not create surface: " << SDL_GetError() << '\n';
    }
    //  SDL_Delay(1000);
    // SDL_DestroyRenderer(renderer);
    // SDL_DestroyWindow(window);
    SDL_Quit();
    ret = 0;
  }
  return ret;
}
