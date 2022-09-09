#include "change_ending.hh"

std::string change_ending(std::string fn, const char *ending) {
  fn = fn.substr(0, fn.rfind('.')) + '.' + ending;
  return fn;
}
