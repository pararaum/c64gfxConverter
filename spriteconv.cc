/*! \file Simple program to convert an image to a C64 sprite.
 *
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

struct SpriteData {
  int pixel[24][21];

  std::ostream &write_asm(std::ostream &out) {
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
    out << "\t.byte $81";
    return out;
  }
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
 * \param 
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


void convert_sprite(SDL_Surface *surface, int x, int y, int border) {
  SpriteData sprite;
  std::map<int,int> pixelvalues;
  int maxpixelval = 0;
  char buf[8];
  
  for(int iy = 0; iy < 21; ++iy) {
    Uint8 *pixelline = static_cast<Uint8 *>(surface->pixels);
    pixelline += (y + iy + border) * surface->pitch;
    for(int ix = 0; ix < 12; ++ix) {
      int pixel = pixelline[border + x + ix];
      sprite.pixel[ix][iy] = pixel;
      if(pixelvalues.find(pixel) == pixelvalues.end()) {
	pixelvalues[pixel] = maxpixelval++;
      }
      //std::cout << ' ' << pixel << '|' << pixelvalues[pixel];
    }
    //std::cout << '\n';
  }
  if(pixelvalues.size() > 4) {
    throw std::logic_error("too many pixel values");
  }
  for(int iy = 0; iy < 21; ++iy) {
    std::cout << "\t.byte";
    for(int ix = 0; ix < 12; ix += 4) {
      int pixel = pixelvalues[sprite.pixel[ix][iy]] << 6
	| pixelvalues[sprite.pixel[ix + 1][iy]] << 4
	| pixelvalues[sprite.pixel[ix + 2][iy]] << 2
	| pixelvalues[sprite.pixel[ix + 3][iy]] << 0
	;
      sprintf(buf, " $%02X%c", pixel, ix < 8 ? ',' : ' ');
      std::cout << buf;
    }
    std::cout << '\n';
  }
  std::cout << boost::format("\t.byte $00\t\t ;; In original image at position $%04x.\n") % y;
}

void extract_sprite_data(SDL_Surface *surface, const gengetopt_args_info *args) {
  SDL_LockSurface(surface);
  if(surface->format->BitsPerPixel != 8) {
    std::cerr << "Unknown pixel format!\n";
  } else {
    if(args->multi_mode_counter == 0) { //No multicolour sprite
      auto sprite = convert_bw_sprite(surface, args->x_position_arg, args->y_position_arg, args->transparent_arg);
      sprite.write_asm(std::cout) << std::endl;
    } else { //Multicolour
      convert_sprite(surface, args->x_position_arg, args->y_position_arg, 0);
    }
  }
  SDL_UnlockSurface(surface);
}


int main(int argc, char **argv) {
  SDL_Window *window;
  SDL_Renderer *renderer;
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
    std::cerr << "Usage: spriteconv [options] <imagename>\n";
    ret = 1;
  } else if(SDL_Init(SDL_INIT_VIDEO) == -1) {
    std::cerr << "SDL_Init() failed: " << SDL_GetError() << std::endl;
    ret = 2;
  } else {
    if(SDL_CreateWindowAndRenderer(320, 200, 0, &window, &renderer) < 0) {
      std::cerr << "SDL_CreateWindowAndRenderer() failed: " << SDL_GetError() << std::endl;
      ret = 3;
    } else {
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
      SDL_Delay(1000);
      // SDL_DestroyRenderer(renderer);
      // SDL_DestroyWindow(window);
    }
    SDL_Quit();
    ret = 0;
  }
  return ret;
}
