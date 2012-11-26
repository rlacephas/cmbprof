//===- TStream.h - Multi-ouput raw_ostream ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Container of verbosity-level regulated raw_ostreams for parallel output
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_TRANSFORMS_FDO_FDOINLINER_TSTREAM_H
#define LLVM_TRANSFORMS_FDO_FDOINLINER_TSTREAM_H

#include "llvm/Support/raw_ostream.h"
#include <vector>

namespace llvm {
  
  namespace vl {
    // stream priorities:         print any message with >= priority
    // message output priorities: print on all streams with <= priority
    //   eg: myTStream.addStream(errs(), vl::error);// (only errors)
    //   eg: myTStream.addStream(outs(), vl::info); // (additional info)
    //   eg: myTStream(vl::log)   << "log message"; // prints on outs() only
    //   eg: myTStream(vl::never) << "useless msg"; // prints on neither
    //   eg: myTStream(vl::error) << "wtf!!";       // prints on both

    const unsigned error = 10;  // errors should always print
    const unsigned always = 10; // always print, from the perpective of the msg
    const unsigned warn = 8;
    const unsigned log = 6;
    const unsigned info = 4;    // 
    const unsigned trace = 3;   // entry/exit of big function, algorithm points
    const unsigned detail = 2;  // tracing into small functions, etc.
    const unsigned verbose = 1; // verbose details: almost never wanted
    const unsigned never = 0;   // never print, from the perpective of the msg
  }

  class TStream {
    
  public:
    TStream(unsigned vl = vl::verbose, bool override = false);
    TStream(llvm::raw_ostream* s, unsigned vl = vl::verbose);
    ~TStream() {};
    
    bool addStream(llvm::raw_ostream* s, unsigned vl = 0);
    void setDefaultPriority(unsigned p);

    void flush();

    // allow verbosity level override/reset
    TStream& operator()(unsigned vl) {V = vl; return(*this);};
    TStream& operator()(void) {V = initV; return(*this);};

    TStream &operator<<(char C);    
    TStream &operator<<(unsigned char C);
    TStream &operator<<(unsigned long N);
    TStream &operator<<(long N);
    TStream &operator<<(unsigned long long N);
    TStream &operator<<(long long N);
    TStream &operator<<(const void *P);
    TStream &operator<<(unsigned int N);
    TStream &operator<<(int N);
    TStream &operator<<(double N);
    TStream &operator<<(const char *Str);
    TStream &operator<<(const std::string &Str);

    // Formatted output, see the format() function in Support/Format.h.
    TStream &operator<<(const format_object_base &Fmt);
    // indent - Insert 'NumSpaces' spaces.
    TStream &indent(unsigned NumSpaces);
    

  private:
    unsigned initV;
    unsigned V;  // verbosity level
    std::vector< std::pair<llvm::raw_ostream*, unsigned> > streams;
    
  }; // class TStream
  
} // namespace llvm

#endif
