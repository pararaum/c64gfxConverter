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


/*! write only the frames as binary output
 *
 * \param outputname output file name
 * \param framearr frames to convert
 * \param startaddr optionally write this start address to the output file
 * \param singfram single frame option
 * \param xorp xor with previous frame if true
 */
void mode_binary_output(const char *outputname, const FrameArray &framearr, std::optional<unsigned short> startaddr, bool singfram, bool xorp) {
  string basename(outputname);
  /* Write startaddr if given */
  auto writestart = [startaddr](std::ostream &out) {
    if(startaddr) {
      auto const stad = startaddr.value();
      out.put(stad & 0xff);
      out.put((stad >> 8) & 0xff);
    }
  };

  if(startaddr) {
    cerr << "Start address is specified as: " << startaddr.value() << endl;
  }
  std::for_each(basename.begin(), basename.end(),
		[](char &c) {
		  if(!isalnum(c)) {
		    c = '_';
		  }
		});
  if(singfram) {
    std::ostringstream asmout;
    std::vector<std::string> labels;
    for(unsigned frame = 0; frame < framearr.size(); ++frame) {
      string outnam(str(boost::format("%s.%04u") % outputname % frame));
      string outlabel(str(boost::format("%s_%04u") % basename % frame));
      labels.push_back(outlabel);
      cerr << "Writing frame " << frame << endl;
      std::ofstream output(outnam, std::ios::binary);
      if(xorp) {
	unsigned previousidx;
	if(frame == 0) {
	  previousidx = framearr.size() - 1;
	} else {
	  previousidx = frame - 1;
	}
	Frame previous(framearr[frame]);
	previous ^= framearr[previousidx];
	previous.save(output);
      } else {
	framearr[frame].save(output);
      }
      asmout << boost::format("%s:\n\t.incbin\t\"%s\"\n") % outlabel % outnam;
    }
    cout << "\t.word\t";
    for(auto it = std::cbegin(labels); it != std::cend(labels); ++it) {
      if(it != std::cbegin(labels)) {
	cout << ", ";
      }
      cout << *it;
    }
    cout << "\n\t.word\t0\n";
    cout << asmout.str() << endl;
  } else {
    std::ofstream output(outputname, std::ios::binary);
    writestart(output);
    if(xorp) {
      throw std::invalid_argument("xorp not yet implemented");
    }
    for(auto &frame : framearr.frames) {
      string offset = frame.name + "_offset";
      cout << offset << " = " << output.tellp() << endl;
      cout << frame.name << "_addr = " << basename << "_base + " << offset << endl;
      frame.save(output);
    }
    cout << basename << "_end = " << output.tellp() << endl;
  }
}


class CodeGenerator {
  std::ostringstream codeout;
  std::ostringstream dataout;
  unsigned framecounter; //!< counter for the animation
  unsigned labelcounter;
  std::string animation_name; //!< name to use for this animation (to generate labels)
  const Frame &initial_frame;
  std::vector<std::string> exports; //!< list of labels to be exported

protected:
  CodeGenerator &opcode(const std::string &mnemonic) {
    codeout << '\t' << mnemonic << '\n';
    return *this;
  }
  CodeGenerator &opcode(boost::format &bformat) {
    return opcode(str(bformat));
  }
  std::ostream &label(const std::string &name) {
    codeout << name << ":\n";
    return codeout;
  }
  std::string animlabel(const std::string &name, bool count_frame = false) {
    std::ostringstream out;
    out << "animation_" << animation_name << '_' << name;
    if(count_frame) {
      out << ++framecounter;
    }
    auto ret(out.str());
    label(ret);
    return ret;
  }
  std::string nextlabel(bool codelabel) {
    auto label = str(boost::format("%s_label%04X") % animation_name % labelcounter++);
    if(codelabel) {
      codeout << '\n' << label << ":\n";
    } else {
      dataout << '\n' <<  label << ":\n";
    }
    return label;
  }
  std::ostream &outbyte(int byte) {
    dataout << "\t.byte\t" << byte << '\n';
    return dataout;
  }
  unsigned long outbytes(const std::vector<int> bytes) {
    unsigned long count = 0;
    for(auto i : bytes) {
      if(count++ % 128 == 0) {
	dataout << "\n\t.byte\t" << i;
      } else {
	dataout << ", " << i;
      }
    }
    dataout << '\n';
    return count;
  }
  
public:
  CodeGenerator(const std::string &name, const Frame &initial) :
    framecounter(0),
    labelcounter(0),
    animation_name(name),
    initial_frame(initial) {
  }
  void generate(const Frame &frame) {
    exports.push_back(animlabel("frame", true));
    for(unsigned i = 0; i < frame.chars.size(); ++i) {
      if(frame.chars[i] != 0) {
	opcode(boost::format("lda #%d") % frame.chars[i])
	  .opcode(boost::format("eor ANIMATIONSCREEN+%d") % i)
	  .opcode(boost::format("sta ANIMATIONSCREEN+%d") % i);
      }
    }
    opcode("rts");
  }
  std::ostream &write(std::ostream &out) {
    exports.push_back(animlabel("init"));
    auto framecharlabel(nextlabel(false));
    outbytes(initial_frame.chars);
    auto framecollabel(nextlabel(false));
    outbytes(initial_frame.colors);
    opcode(boost::format("lda #%d") % initial_frame.background)
      .opcode("sta $d021");
    opcode(boost::format("lda #%d") % initial_frame.border)
      .opcode("sta $d020");
    auto looplabel(nextlabel(true));
    codeout << boost::format(R"(	ldx #0
	.repeat 4,I
	 lda %s+I*250,x
	 sta ANIMATIONSCREEN+I*250,x
	 lda %s+I*250,x
	 sta $D800+I*250,x
	.endrepeat
	inx
	cpx #250
	bne %s
)") % framecharlabel % framecollabel % looplabel ;
    opcode("rts");
    // Now write:
    out << "\t.import\tANIMATIONSCREEN\n";
    for(auto label : exports) {
      out << "\t.export\t" << label << '\n';
    }
    out << "\t.rodata\n";
    out << dataout.str() << '\n';
    out << "\t.code\n";
    out << codeout.str() << '\n';
    return out;
  }
};

/*! Generate complete (self-contained) code for the animation
 *
 * \param framearr the array of frames
 */
void mode_generate_code(const FrameArray &framearr) {
  unsigned frameidx;

  if(framearr.size() < 2) {
    throw std::invalid_argument("not enough frames");
  }
  CodeGenerator generator("petscii", framearr[0]);
  for(frameidx = 0; frameidx < framearr.size() - 1; ++frameidx) {
    Frame framedata(framearr[frameidx]);
    framedata ^= framearr[frameidx + 1]; //XOR to find the changing areas.
    generator.generate(framedata);
  }
  generator.write(std::cout);
}


/*! main code
 *
 * \param argc number of cli arguments
 * \param argv command line parameters
 * \return 0 if ok
 */
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
    if(args_info.last_given) {
      if(static_cast<unsigned int>(args_info.last_arg) >= framearr.size()) {
	cerr << "Error! Last frame bigger than available frames.\n";
	return 3;
      }
      framearr.erase(framearr.begin() + args_info.last_arg + 1, framearr.end());
      cerr << "Only up to frame: " << args_info.last_arg << endl;
    }
    if(args_info.first_given) {
      if(static_cast<unsigned int>(args_info.first_arg) >= framearr.size()) {
	cerr << "Error! First frame bigger than available frames.\n";
	return 3;
      }
      framearr.erase(framearr.begin(), framearr.begin() + args_info.first_arg);
      cerr << "From frame: " << args_info.first_arg << endl;
    }
  }
  catch(const std::invalid_argument &excp) {
    cerr << "Parsing failed: " << excp.what() << std::endl;
    return 2;
  }
  cerr << "Found " << framearr.size() << " frames.\n";
  if(args_info.output_bin_given) { // use binary output mode
    std::optional<unsigned short> startaddr;
    if(args_info.start_addr_given) {
      startaddr = args_info.start_addr_arg;
    }
    mode_binary_output(args_info.output_bin_arg, framearr, startaddr, args_info.separate_frame_given, args_info.xor_previous_given);
  } if(args_info.generate_code_given) { // generate code mode
    mode_generate_code(framearr);
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
