/*! \file Simple program to convert an image to a C64 sprite.
 *
 */

#include <iostream>
#include <getopt.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <map>
#include <boost/format.hpp>
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
};

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

void extract_sprite_data(SDL_Surface *surface) {
  SDL_LockSurface(surface);
  if(surface->format->BitsPerPixel != 8) {
    std::cerr << "Unknown pixel format!\n";
  } else {
    //   for(int y = 0; y < surface->h; ++y) {
    //     for(int x = 0; x < surface->w; ++x) {
    // 	sprintf(buf, "%02X", static_cast<Uint8 *>(surface->pixels)[surface->pitch * y + x]);
    // 	std::cout << buf;
    //     }
    //     std::cout << std::endl;
    //   }
  }
  for(int y = 0; y < surface->h - 21; y += 21 + 1) {
    convert_sprite(surface, 0, y, 1);
  }
  SDL_UnlockSurface(surface);
}


int main(int argc, char **argv) {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Surface *surface;
  gengetopt_args_info args_info;
  int ret = -1;

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
    if(SDL_CreateWindowAndRenderer(800, 800, 0, &window, &renderer) < 0) {
      std::cerr << "SDL_CreateWindowAndRenderer() failed: " << SDL_GetError() << std::endl;
      ret = 3;
    } else {
      surface = IMG_Load(argv[1]);
      if(surface) {
	SDL_SetWindowTitle(window, argv[1]);
	SDL_SetWindowSize(window, surface->w, surface->h);
	SDL_ShowWindow(window);
	display_image(surface, renderer);
	SDL_RenderPresent(renderer);
	extract_sprite_data(surface);
	SDL_FreeSurface(surface);
      } else {
	std::cerr << "Can not create surface!\n";
      }
      SDL_Delay(1000);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
    }
    SDL_Quit();
    ret = 0;
  }
  return ret;
}
