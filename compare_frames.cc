#include "petsciiframes.hh"
#include <limits>
#include <cassert>

std::vector<std::optional<std::pair<unsigned, unsigned>>> compare_frames(const Frame &prev, const Frame &next) {
  std::vector<std::optional<std::pair<unsigned, unsigned>>> ret;

  for(unsigned row = 0; row < HEIGHT; ++row) {
    const unsigned umax = std::numeric_limits<unsigned>::max();
    unsigned left = umax;
    unsigned right = umax;
    auto prevleft = prev.chars_row(row);
    auto nextleft = next.chars_row(row);
    for(unsigned x = 0; x < WIDTH; ++x) {
      if((left == umax) && prevleft[x] != nextleft[x]) {
	left = x;
      }
      if((right == umax) && prevleft[WIDTH - x - 1] != nextleft[WIDTH - x - 1]) {
	right = WIDTH - x - 1;
      }
    }
    if(left == umax) { // No mismatch
      ret.push_back(std::nullopt);
    } else {
      assert(right != umax); // We have found no mismatch above so
			     // there should be none if we are coming
			     // from the right...
      /*cout << "row=" << row
	   << ": min=" << left
	   << ": max=" << right
	   << endl;*/
      ret.push_back(std::make_pair(left, right));
    }
  }
  assert(ret.size() == HEIGHT);
  return ret;
}

