//===- llvm-cprof.cpp - Read in and process llvmprof.out data files --------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tools is meant to combine a set of profiles into common combined
// edge and path profiling files.
//
//===----------------------------------------------------------------------===//

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Analysis/CombinedProfile.h"
#include "llvm/Analysis/CPFactory.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Signals.h"

#include <cstdio>
#include <vector>
#include <map>

#define VERBOSE(s) if( Verbose ) { s; }

using namespace llvm;

namespace {
	// Uninstrumented bitcode file
	cl::opt<std::string> BitcodeFile(cl::Positional,
		cl::desc("<program bitcode file>"), cl::Required);

	// Combined profiling cumulative storage file
	cl::opt<std::string> CPOutFile("cpFile",
		cl::init("combined.cp"), cl::value_desc("filename"),
		cl::desc("Combined edge profiling cumulative storage file."));

	// Verbose: output specifics regarding which edges/ paths are merged
	cl::opt<bool> 
  Verbose("v", cl::init(false),
          cl::desc("Verbose output."));

	// Profiling files to be merged into the "master" combined profiling files
	cl::list<std::string> InputFilenames(cl::Positional, cl::OneOrMore,
		cl::desc("<input edge/path files>"));

  // ---------------------------------------------------------------------------
 
  // load a module's bitcode into memory
	Module* loadModule() 
  {
		LLVMContext &Context = getGlobalContext();
    Module* M = NULL;
    
		// Read in the bitcode file ...
		std::string ErrorMessage;
		if (MemoryBuffer *Buffer = MemoryBuffer::getFileOrSTDIN(BitcodeFile,
                                                            &ErrorMessage)) 
    {
			M = ParseBitcodeFile(Buffer, Context, &ErrorMessage);
			delete Buffer;
		}

		// Ensure the module has been loaded
		if (M == NULL)
			errs() << BitcodeFile << ": " << ErrorMessage << "\n";

    VERBOSE(errs() << "Finished processing bitcode\n");
		return(M);
	}

} // namespace

int main(int argc, char *argv[]) 
{
  
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  
  // Call llvm_shutdown() on exit.
  llvm_shutdown_obj Y;
  
  // Setup command line arguments
  cl::ParseCommandLineOptions(argc, argv,
                              "llvm combined profile builder.\n");
  
  // Get access to the current module
  Module* currentModule = loadModule();
  if( currentModule == NULL ) return 1;
  
  CPFactory fact = CPFactory(*currentModule); 
  
  // build the combined profile(s)
  if( !fact.buildProfiles(InputFilenames) )
  {
    errs() << "Failed to read profiles\n";
    return(-1);
  }
  
  
  //
  // Combined Profile Output
  //
  
  FILE* file = fopen(CPOutFile.c_str(),"wb");
  if (!file) 
  {
    errs() << "  error: cannot open '" << CPOutFile.c_str() 
           << "' for writing.\n";
    return -1;
  }
  
  // Write the combined edge profile
  if(fact.hasEdgeCP())
  {
    CombinedEdgeProfile* cepOut = fact.takeEdgeCP();
    VERBOSE(errs() << "CEP: " << cepOut->size() << " edges\n");
    VERBOSE(errs() << "Writing combined edge profile to '" 
            << CPOutFile.c_str() << "'\n");
    unsigned written = cepOut->serialize(file);
    VERBOSE(errs() << "CEP: wrote " << written << " histograms.\n");
    delete cepOut;
  }
  
  // write the combined path profile
  if(fact.hasPathCP())
  {
    CombinedPathProfile* cppOut = fact.takePathCP();
    VERBOSE(errs() << "CPP: " << cppOut->getFunctionCount() 
            << " functions, " << cppOut->size() << "paths\n");
    VERBOSE(errs() << "Writing combined path profile to '" 
            << CPOutFile.c_str() << "'\n");
    unsigned written = cppOut->serialize(file);
    VERBOSE(errs() << "CPP: wrote " << written << " histograms.\n");
    delete cppOut;
  }
  
  // write the combined call profile
  if(fact.hasCallCP())
  {
    CombinedCallProfile* ccpOut = fact.takeCallCP();
    VERBOSE(errs() << "CCP: " << ccpOut->size() << " BBs with calls\n");
    VERBOSE(errs() << "Writing combined call profile to '" 
            << CPOutFile.c_str() << "'\n");
    unsigned written = ccpOut->serialize(file);
    VERBOSE(errs() << "CCP: wrote " << written << " histograms.\n");
    delete ccpOut;
  }
  
  fclose(file);
  
  // clean up loaded module
  if (currentModule)
    delete currentModule;

  CPFactory::freeStaticData();
  
  return(0);
  
} // main

