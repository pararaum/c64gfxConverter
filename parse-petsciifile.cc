#include "parse-petsciifile.hh"
#include "petsciiframes.hh"
#include "cpp-peglib/peglib.h"
#include <iostream>

FrameArray parse_file(std::istream &inp) {
  std::vector<Frame> frames;
  int width = -1;
  int height = -1;
  peg::parser parser(R"(
        # Parse PETSCII C
        file <- ( frame )+ COMMENT?
  	frame <- 'unsigned char' FRAMENAME '[' ']' '=' BRACELEFT COMMENT? data BRACERIGHT SEMICOLON
  	data <- NUMBER (',' NUMBER)*
        # Terminal symbols
        ## These may return different things.
        NUMBER <- < [0-9]+ >
        FRAMENAME <- < [a-zA-Z_][0-9a-zA-Z_]* >
  	BRACELEFT <- '{'
  	BRACERIGHT <- '}'
	SEMICOLON <- ';'
  	COMMENT <- '//' META? [^\n]* NL
        META <- 'META:' NUMBER NUMBER
  	NL <- '\n'
        # Other
        %whitespace <- [ \t\r\n]*
  )");

  // Check if grammer is ok.
  assert(static_cast<bool>(parser));
  // Add a function for outputting errors.
  parser.set_logger([](size_t line, size_t col, const std::string& msg) {
      std::cerr << "Error: " << line << ":" << col << ": " << msg << "\n";
    });
  
  parser["META"] =
    [&width, &height](const peg::SemanticValues &vs) {
    width = std::any_cast<int>(vs[0]);
    height = std::any_cast<int>(vs[1]);
  };
  parser["frame"] =
    [&frames](const peg::SemanticValues &vs) {
    Frame frame;
    std::string name(std::any_cast<std::string>(vs[0]));
    switch(vs.size()) {
    case 5:
      // No comment, so this is at position 2.
      frame = Frame(name, std::any_cast<std::vector<int>>(vs[2]));
      break;
    case 6:
      // With comment, so position 3;
      frame = Frame(name, std::any_cast<std::vector<int> >(vs[3]));
      break;
    default:
      throw std::logic_error("unknown value in switch at frame");
    }
    frames.push_back(frame);
    return frame;
  };
  parser["data"] =
    [](const peg::SemanticValues &vs) {
    std::vector<int> data;
    data.resize(vs.size());
    for(unsigned i = 0; i < vs.size(); ++i) {
      data[i] = std::any_cast<int>(vs[i]);
    }
    return data;
  };
  parser["NUMBER"] =
    [](const peg::SemanticValues &vs) {
    return vs.token_to_number<int>();
  };
  parser["FRAMENAME"] =
    [](const peg::SemanticValues &vs) {
    std::string fname("_"); // Underscore for assembler.
    fname += vs.token_to_string();
    return fname;
  };
  
  // Prepare parse
  parser.enable_packrat_parsing(); // Enable packrat parsing.
  // Read into string.
  std::ostringstream contents;
  contents << inp.rdbuf();
  // Parse!
  auto ret = parser.parse(contents.str());
  if(!ret) {
    throw std::invalid_argument("parsing failed");
  } else {
    return FrameArray{width, height, frames};
  }
  throw std::logic_error("never reached");
}
