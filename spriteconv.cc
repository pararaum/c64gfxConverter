/*! \file spriteconv.cc
 *  \brief Convert an image to C64 sprite assembly data.
 *
 * Every sprite has 63 bytes of sprite data; the last byte contains metadata.
 *
 * Sprite layout reference: https://csdb.dk/forums/?roomid=7&topicid=125812
 * Multicolour bit patterns: http://codebase64.org/doku.php?id=base:spriteintro
 *
 *   - 00: transparent
 *   - 01: $d025  (multicolour 1)
 *   - 11: $d026  (multicolour 2)
 *   - 10: individual colour ($d027-$d02f)
 *
 * ### Usage
 *
 * ```
 * spriteconv [COMMON OPTIONS] mono  <file>
 * spriteconv [COMMON OPTIONS] multi [MULTI OPTIONS] <file>
 * ```
 *
 * Build dependencies: SDL2, SDL2_image, CLI11
 * Requires: C++23 (-std=c++23)
 */

#include <cassert>
#include <format>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <CLI/CLI.hpp>

#define VERSION "1.0"

// ── sprite data structures ────────────────────────────────────────────────────

/// Common interface for sprite types.
struct SpriteInterface {
  virtual std::ostream &write_asm(std::ostream &out) const = 0;
  virtual int width()  const = 0;
  virtual int height() const = 0;
  virtual ~SpriteInterface() = default;
};

/// Standard (1-bit) 24x21 pixel sprite.
struct SpriteData : public SpriteInterface {
  int pixel[24][21];

  std::ostream &write_asm(std::ostream &out) const override {
    for (int iy = 0; iy < 21; ++iy) {
      out << "\t.byte";
      for (int ix = 0; ix < 24; ix += 8) {
        unsigned int value = 0;
        for (int bit = 0; bit < 8; ++bit)
          if (pixel[ix + bit][iy] != 0)
            value |= 1u << (7 - bit);
        out << std::format(" ${:02X}{}", value, ix < 16 ? ',' : ' ');
      }
      out << '\n';
    }
    out << "\t.byte $00";
    return out;
  }

  int width()  const override { return 24; }
  int height() const override { return 21; }
};

/*! \brief Multicolour 12x21 sprite (each logical pixel = 2 screen pixels wide).
 *
 * Pixel values store the 2-bit C64 colour pattern (0-3) per logical pixel.
 */
struct MultiSpriteData : public SpriteInterface {
  int pixel[12][21];

  std::ostream &write_asm(std::ostream &out) const override {
    for (int iy = 0; iy < 21; ++iy) {
      out << "\t.byte";
      for (int ix = 0; ix < 12; ix += 4) {
        unsigned int value = 0;
        for (int bit = 0; bit < 8; bit += 2)
          value |= (pixel[ix + bit / 2][iy] & 3) << (6 - bit);
        out << std::format(" ${:02X}{}", value, ix < 8 ? ',' : ' ');
      }
      out << '\n';
    }
    out << "\t.byte $80";
    return out;
  }

  int width()  const override { return 12; }
  int height() const override { return 21; }
};

// ── pixel helpers ─────────────────────────────────────────────────────────────

/**
 * \brief Read one palette index byte from an 8-bit-per-pixel SDL surface.
 *
 * \param surface  8 bpp surface (asserted, not checked).
 * \param x        Column.
 * \param y        Row.
 * \return Palette index at (x, y).
 */
int get_color(SDL_Surface *surface, int x, int y) {
  assert(x >= 0 && y >= 0);
  const Uint8 *row = static_cast<const Uint8 *>(surface->pixels)
                   + y * surface->pitch;
  return row[x];
}

// ── display ───────────────────────────────────────────────────────────────────

/**
 * \brief Display an SDL surface in a window and wait for the user to close it.
 *
 * Creates a window sized to the surface, uploads the surface as a texture
 * (SDL handles palette expansion automatically), and runs an event loop until
 * the user presses any key or closes the window.
 *
 * \note Requires \c SDL_INIT_VIDEO to have been initialised before calling.
 *
 * \param surface   The surface to display.
 * \param title     Window title string.
 * \return true on clean exit, false if an SDL error occurred.
 */
bool display_surface(SDL_Surface *surface, const std::string &title) {
  SDL_Window *window = SDL_CreateWindow(
    title.c_str(),
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    surface->w, surface->h,
    SDL_WINDOW_SHOWN
  );
  if (!window) {
    std::cerr << std::format("SDL_CreateWindow() failed: {}\n", SDL_GetError());
    return false;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) {
    std::cerr << std::format("SDL_CreateRenderer() failed: {}\n", SDL_GetError());
    SDL_DestroyWindow(window);
    return false;
  }

  /* SDL_CreateTextureFromSurface expands the palette automatically, so an
   * 8 bpp indexed surface is displayed with its correct colours without any
   * manual conversion. */
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (!texture) {
    std::cerr << std::format("SDL_CreateTextureFromSurface() failed: {}\n", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return false;
  }

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);

  /* Event loop: keep the window alive until keypress or close. */
  SDL_Event event;
  bool running = true;
  while (running) {
    /* SDL_WaitEvent blocks until an event arrives, avoiding a busy-spin. */
    if (SDL_WaitEvent(&event) == 0) {
      std::cerr << std::format("SDL_WaitEvent() failed: {}\n", SDL_GetError());
      break;
    }
    switch (event.type) {
      case SDL_QUIT:
      case SDL_KEYDOWN:
        running = false;
        break;
      default:
        break;
    }
  }

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  return true;
}

// ── sprite conversion ─────────────────────────────────────────────────────────

/*! \brief Convert a region of an 8 bpp surface to a 1-bit sprite.
 *
 * \param surface      Source surface (must be 8 bpp).
 * \param xpos         X offset of the sprite's top-left corner.
 * \param ypos         Y offset of the sprite's top-left corner.
 * \param transparent  Palette index treated as transparent (= 0 bit).
 */
SpriteData convert_bw_sprite(SDL_Surface *surface,
                             int xpos, int ypos,
                             int transparent)
{
  SpriteData sprite;
  for (int row = 0; row < 21; ++row)
    for (int column = 0; column < 24; ++column)
      sprite.pixel[column][row] =
        (get_color(surface, xpos + column, ypos + row) != transparent) ? 1 : 0;
  return sprite;
}

/*! \brief Convert a region of an 8 bpp surface to a multicolour sprite.
 *
 * \param surface      Source surface (must be 8 bpp).
 * \param xpos         X offset of the sprite's top-left corner.
 * \param ypos         Y offset of the sprite's top-left corner.
 * \param transparent  Palette index for transparent pixels (bit pattern 00).
 * \param m1           Palette index for multicolour-1 pixels (bit pattern 01).
 * \param m2           Palette index for multicolour-2 pixels (bit pattern 11).
 *                     All other palette indices map to individual colour (10).
 */
MultiSpriteData convert_multi_sprite(SDL_Surface *surface,
                                     int xpos, int ypos,
                                     int transparent, int m1, int m2)
{
  MultiSpriteData sprite;
  for (int row = 0; row < 21; ++row) {
    for (int column = 0; column < 12; ++column) {
      const int px = get_color(surface, xpos + column, ypos + row);
      int pattern;
      if      (px == transparent) pattern = 0b00;
      else if (px == m1)          pattern = 0b01;
      else if (px == m2)          pattern = 0b11;
      else                        pattern = 0b10;
      sprite.pixel[column][row] = pattern;
    }
  }
  return sprite;
}

/*! \brief Auto-assign up to 4 colours from a sprite region (multicolour).
 *
 * Colours are assigned ordinal values (0-3) in first-occurrence order.
 * Throws \c std::logic_error if more than 4 distinct palette indices appear.
 *
 * \param surface  Source surface (must be 8 bpp).
 * \param x        X offset of the sprite's top-left corner.
 * \param y        Y offset of the sprite's top-left corner.
 * \param border   Additional border offset added to both axes.
 */
MultiSpriteData convert_sprite(SDL_Surface *surface, int x, int y, int border) {
  MultiSpriteData sprite;
  std::map<int, int> pixelvalues;
  int maxpixelval = 0;

  for (int iy = 0; iy < 21; ++iy) {
    const Uint8 *pixelline = static_cast<const Uint8 *>(surface->pixels)
                           + (y + iy + border) * surface->pitch;
    for (int ix = 0; ix < 12; ++ix) {
      const int px = pixelline[border + x + ix];
      auto [it, inserted] = pixelvalues.emplace(px, maxpixelval);
      if (inserted) ++maxpixelval;
      sprite.pixel[ix][iy] = it->second;
    }
  }

  if (pixelvalues.size() > 4)
    throw std::logic_error("too many pixel values");

  return sprite;
}

// ── CLI options struct ────────────────────────────────────────────────────────

/**
 * \brief Aggregated command-line options.
 *
 * All fields mirror the original gengetopt option names for easy diffing.
 */
struct Options {
  // common options
  std::string                input_file;          ///< Positional: image file (owned by subcommand)
  int                        x_position    = 0;   ///< --x-position / -x
  int                        y_position    = 0;   ///< --y-position / -y
  int                        transparent   = 0;   ///< --transparent / -t
  std::optional<std::string> labelname;            ///< --labelname (optional)
  int                        spritecolumns = 1;   ///< --spritecolumns / -c
  int                        spriterows    = 1;   ///< --spriterows / -r
  int                        columnwidth   = 24;  ///< --columnwidth / -W
  int                        rowheight     = 21;  ///< --rowheight / -H
  bool                       display       = false; ///< --display / -d

  // multicolour subcommand options
  bool multi_mode = false; ///< true when the "multi" subcommand was selected
  int  multi1     = 1;     ///< --multi1 / -1  (bit pattern 01)
  int  multi2     = 2;     ///< --multi2 / -2  (bit pattern 11)
  bool autocol    = false; ///< --autocol (auto-choose colours)
};

// ── sprite extraction ─────────────────────────────────────────────────────────

/**
 * \brief Iterate over a sprite sheet and write assembly for every sprite.
 *
 * \param surface  Source SDL surface (must be 8 bpp, locked by this function).
 * \param opts     Parsed command-line options.
 */
void extract_sprite_data(SDL_Surface *surface, const Options &opts) {
  if (surface->format->BitsPerPixel != 8) {
    std::cerr << "Unknown pixel format!\n";
    return;
  }

  SDL_LockSurface(surface);

  const int rowheight   = opts.rowheight;
  const int columnwidth = opts.columnwidth;

  for (int y = 0;
       (opts.spriterows > 0 ? y < opts.spriterows
                            : y * rowheight + opts.y_position < surface->h - rowheight);
       ++y)
  {
    for (int x = 0;
         (opts.spritecolumns > 0 ? x < opts.spritecolumns
                                 : x * columnwidth + opts.x_position < surface->w - columnwidth);
         ++x)
    {
      const int x_position = x * columnwidth + opts.x_position;
      const int y_position = y * rowheight   + opts.y_position;

      if (opts.labelname)
        std::cout << std::format("{}{}{}: \n", *opts.labelname, x, y);

      if (!opts.multi_mode) {
        convert_bw_sprite(surface, x_position, y_position, opts.transparent)
          .write_asm(std::cout) << '\n';
      } else {
        MultiSpriteData sprite = opts.autocol
          ? convert_sprite(surface, x_position, y_position, 0)
          : convert_multi_sprite(surface, x_position, y_position,
                                 opts.transparent, opts.multi1, opts.multi2);
        sprite.write_asm(std::cout) << '\n';
      }
    }
  }

  SDL_UnlockSurface(surface);
}

// ── entry point ───────────────────────────────────────────────────────────────

/**
 * \brief Program entry point.
 *
 * ### CLI structure
 *
 * Exactly one subcommand must be given.  Common options (including --display)
 * are registered on the top-level app and accepted before the subcommand name:
 *
 * ```
 * spriteconv [COMMON OPTIONS] mono  <file>
 * spriteconv [COMMON OPTIONS] multi [MULTI OPTIONS] <file>
 * ```
 *
 * ### SDL initialisation
 *
 * \c SDL_INIT_VIDEO is only requested when \c --display is active; without it
 * only the bare minimum needed for \c IMG_Load is initialised.
 *
 * \param argc  Argument count.
 * \param argv  Argument vector.
 * \return 0 on success, non-zero on error.
 */
int main(int argc, char **argv) {
  CLI::App app{"spriteconv -- convert images to C64 sprites"};
  app.set_version_flag("--version", VERSION);
  app.require_subcommand(1);

  Options opts;

  // ── common options ────────────────────────────────────────────────────────
  app.add_option("--x-position,-x", opts.x_position,
                 "X position of sprite in image")
     ->default_val(0);
  app.add_option("--y-position,-y", opts.y_position,
                 "Y position of sprite in image")
     ->default_val(0);
  app.add_option("--transparent,-t", opts.transparent,
                 "Transparent colour index")
     ->default_val(0);
  app.add_option("--labelname", opts.labelname,
                 "Name to use for the label where sprite data starts");
  app.add_option("--spritecolumns,-c", opts.spritecolumns,
                 "Number of sprite columns in the sprite sheet "
                 "(negative = use whole width)")
     ->default_val(1);
  app.add_option("--spriterows,-r", opts.spriterows,
                 "Number of sprite rows in the sprite sheet "
                 "(negative = use whole height)")
     ->default_val(1);
  app.add_option("--columnwidth,-W", opts.columnwidth,
                 "Width of a single sprite in the sprite sheet (pixels)")
     ->default_val(24);
  app.add_option("--rowheight,-H", opts.rowheight,
                 "Height of a single sprite in the sprite sheet (pixels)")
     ->default_val(21);
  app.add_flag("--display,-d", opts.display,
               "Display the loaded image in a window (press any key to close)");

  // ── "mono" subcommand ─────────────────────────────────────────────────────
  CLI::App *mono_cmd = app.add_subcommand("mono", "Convert monochrome (1-bit) sprites");
  mono_cmd->add_option("file", opts.input_file,
                       "Input image file (8 bpp palette PNG/etc.)")
          ->required()
          ->check(CLI::ExistingFile);

  // ── "multi" subcommand ────────────────────────────────────────────────────
  CLI::App *multi_cmd = app.add_subcommand("multi", "Convert multicolour sprites");
  multi_cmd->add_option("file", opts.input_file,
                        "Input image file (8 bpp palette PNG/etc.)")
           ->required()
           ->check(CLI::ExistingFile);
  multi_cmd->add_option("--multi1,-1", opts.multi1,
                        "Multicolour 1 palette index (bit pattern 01)")
           ->default_val(1);
  multi_cmd->add_option("--multi2,-2", opts.multi2,
                        "Multicolour 2 palette index (bit pattern 11)")
           ->default_val(2);
  multi_cmd->add_flag("--autocol", opts.autocol,
                      "Automatically choose colours (ignores --multi1/--multi2)");

  CLI11_PARSE(app, argc, argv);

  opts.multi_mode = multi_cmd->parsed();

  // ── SDL initialisation ────────────────────────────────────────────────────
  // Only request SDL_INIT_VIDEO when the display window is actually needed;
  // IMG_Load works without it and skipping it avoids opening a display
  // connection in purely batch/headless usage.
  const Uint32 sdl_flags = opts.display ? SDL_INIT_VIDEO : 0;
  if (SDL_Init(sdl_flags) != 0) {
    std::cerr << std::format("SDL_Init() failed: {}\n", SDL_GetError());
    return 2;
  }

  SDL_Surface *surface = IMG_Load(opts.input_file.c_str());
  if (!surface) {
    std::cerr << std::format("Cannot load image '{}': {}\n",
                             opts.input_file, SDL_GetError());
    SDL_Quit();
    return 3;
  }

  if (opts.display)
    display_surface(surface, opts.input_file);

  extract_sprite_data(surface, opts);

  SDL_FreeSurface(surface);
  SDL_Quit();
  return 0;
}
