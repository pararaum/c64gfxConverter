#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cassert>
#include <boost/format.hpp>
#include "petsciiframes.hh"
#include "parse-petsciifile.hh"
#include "petsciiconvert_cli.h"

using std::cout;
using std::cerr;
using std::endl;
using std::string;

std::vector<std::string> do_comparison(const FrameArray &framearr, bool pingpong) {
  std::vector<std::string> names;
  auto localfun = [&framearr,&names](unsigned prevnum, unsigned nextnum) {
    auto &prev = framearr[prevnum];
    auto &next = framearr[nextnum];
    cerr << boost::format("\tComparing %u (%s) to %u (%s).\n") % prevnum % prev.name % nextnum % next.name;
    auto mismatches = compare_frames(prev, next);
    std::string procname = "animation_";
    procname += prev.name;
    procname += next.name;
    cout << "\n\t.proc\t" << procname << "\n";
    names.push_back(procname);
    for(unsigned row = 0; row < HEIGHT; ++row) {
      auto &mismatch = mismatches[row];
      if(mismatch) {
	if(mismatch->first == mismatch->second) {
	  // Only one element.
	  cout << boost::format(R"(	 lda	%s+2+%u*40+%u
	 sta	ANIMATIONSCREEN+%u*40+%u
)") % next.name % row % mismatch->first % row % mismatch->first;
	  cout << boost::format(R"(	 lda	%s+2+%u*%u+%u*40+%u
	 sta	$D800+%u*40+%u
)") % next.name % WIDTH % HEIGHT % row % mismatch->first % row % mismatch->first;
	} else {
	  cout << boost::format(R"(	 ldx	#%u
loop%u:	  lda	%s+2+%u*40+%u,x
	  sta	ANIMATIONSCREEN+%u*40+%u,x
	  lda	%s+2+%u*%u+%u*40+%u,x
	  sta	$D800+%u*40+%u,x
	  dex
	 bpl	loop%u
)") % (mismatch->second - mismatch->first)
	    % row % next.name % row % mismatch->first
	    % row % mismatch->first
	    % next.name % WIDTH % HEIGHT % row % mismatch->first
	    % row % mismatch->first
	    % row;
	}
      }
    }
    cout << "\t rts\n\t.endproc\n";
  };
  for(unsigned framenum = 0; framenum < framearr.size(); ++framenum) {
    localfun(framenum == 0 ? framearr.size() - 1 : framenum - 1, framenum);
  }
  if(pingpong) {
    for(unsigned framenum = framearr.size() - 1; framenum > 0; --framenum) {
      localfun(framenum, framenum - 1);
    }
  }
  return names;
}

void mode_binary_output(const char *outputname, const FrameArray &framearr) {
  std::ofstream output(outputname);
  string basename(outputname);

  std::for_each(basename.begin(), basename.end(),
		[](char &c) {
		  if(!isalnum(c)) {
		    c = '_';
		  }
		});
  for(auto &frame : framearr.frames) {
    string offset = frame.name + "_offset";
    cout << offset << " = " << output.tellp() << endl;
    cout << frame.name << "_addr = " << basename << "_base + " << offset << endl;
    frame.save(output);
  }
  cout << basename << "_end = " << output.tellp() << endl;
}

int main(int argc, char **argv) {
  FrameArray framearr;
  std::istream *in = &std::cin;
  std::ifstream infile;
  gengetopt_args_info args_info;

  auto cli = cmdline_parser(argc, argv, &args_info);
  if(cli != 0) {
    std::cerr << "Error while parsind command line!\n";
    return -1;
  }
  if(args_info.inputs_num >= 1) {
    infile.open(args_info.inputs[0]);
    if(!infile) {
      cerr << "Can not open file " << args_info.inputs[0] << "!\n";
      return 2;
    }
    in = &infile;
  }
  cerr << "Parsing..." << std::flush;
  // Parse!
  try {
    framearr = parse_file(*in);
  }
  catch(const std::invalid_argument &excp) {
    cerr << "Parsing failed: " << excp.what() << std::endl;
    return 2;
  }
  cerr << "Found " << framearr.size() << " frames.\n";
  if(args_info.output_bin_given) { // use binary output mode
    mode_binary_output(args_info.output_bin_arg, framearr);
  } else { // default mode is animation mode
    cout << ";\twidth=" << framearr.width << ", height=" << framearr.height << std::endl;
    cout << "\t.import ANIMATIONSCREEN\n";
    for(auto i : framearr.frames) {
      cout << "\t.import\t" << i.name << endl;
    }
    auto procnames(do_comparison(framearr, args_info.ping_pong_flag));
    cout << endl;
    for(auto i : procnames) {
      cout << "\t.export\t" << i << endl;
    }
  }
  return 0;
}
