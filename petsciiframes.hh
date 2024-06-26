#ifndef __FRAMES_HH_2022__
#define __FRAMES_HH_2022__
#include <string>
#include <vector>
#include <ostream>
#include <stdexcept>
#include <optional>
#define WIDTH 40
#define HEIGHT 25

class Frame {
protected:
  unsigned width;
  unsigned height;
public:
  int border;
  int background;
  std::string name;
  std::vector<int> chars;
  std::vector<int> colors;

  Frame() : width(40), height(25), border(-1), background(-1) {}
  Frame(const std::string &name_, const std::vector<int> &data) : width(40), height(25), border(data.at(0)), background(data.at(1)), name(name_) {
    auto cells(data.size() - 2);
    if(cells & 1) {
      throw std::logic_error("odd number of cells");
    } else {
      cells /= 2;
    }
    chars.resize(cells);
    colors.resize(cells);
    for(unsigned i = 0; i < cells; ++i) {
      chars[i] = data[2 + i];
      colors[i] = data[2 + cells + i];
    }
  }

  /*! save data in binary form
   *
   * Save all frames into a stream in binary form.
   *
   * \param out output stream
   * \return modified output stream
   */
  std::ostream &save(std::ostream &out) const {
    out.put(border);
    out.put(background);
    for(auto c : chars) {
      out.put(c);
    }
    for(auto c : colors) {
      out.put(c);
    }
    return out;
  }
  std::vector<int>::const_iterator chars_row(unsigned row) const {
    //std::advance(chars.cbegin(), row * width);
    return chars.begin() + row * width;
  }
  std::vector<int>::const_iterator colors_row(unsigned row) const {
    //std::advance(colors.cbegin(), row * width);
    return colors .begin() + row * width;
  }
};

/*! \brief function to compare two frames
 *
 * The min and max positions are inclusive so if a single character in
 * a row changed (eg the second) then both min and max will be the
 * same (eg 2 in our example).
 * 
 * \param prev previous frame in the animation
 * \param next next frame in the animation (next > previous)
 * \return vector of min (aka. first)/max (aka second) pairs or boost::none per row
 */
std::vector<std::optional<std::pair<unsigned, unsigned>>> compare_frames(const Frame &prev, const Frame &next);

struct FrameArray {
  int width;
  int height;
  std::vector<Frame> frames;
  std::vector<Frame>::size_type size() const { return frames.size(); };
  std::vector<Frame>::iterator begin() { return frames.begin(); }
  std::vector<Frame>::iterator end() { return frames.end(); }
  std::vector<Frame>::iterator erase(std::vector<Frame>::const_iterator begin, std::vector<Frame>::const_iterator end) {
    return frames.erase(begin, end);
  }
  const Frame &operator[](unsigned i) const { return frames.at(i); };
};

#endif
