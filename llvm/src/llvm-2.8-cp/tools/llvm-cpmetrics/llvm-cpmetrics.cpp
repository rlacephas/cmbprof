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
#include "llvm/Analysis/CPFactory.h"
#include "llvm/Analysis/CombinedProfile.h"
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

	// metric selection
	cl::opt<bool> Stats("stats",
		cl::desc("print histogram statistics"));

	cl::opt<bool> Drift("drift",
		cl::desc("Compute drift between first and second combined profiles"));

	cl::opt<bool> Print("print",
		cl::desc("print the profile"));

	cl::opt<bool> Summary("summary",
		cl::desc("print 1-line summary (default)"));

	// Verbose: output specifics regarding which edges/ paths are merged
	cl::opt<bool>
		Verbose("v",
		cl::desc("Spew extra info"));

	// Profiling files to be merged into the "master" combined profiling files
	cl::list<std::string> InputFilenames(cl::Positional, cl::OneOrMore,
		cl::desc("<input edge/path files>"));

  // ---------------------------------------------------------------------------

  // load a module's bitcode into memory
	Module* loadModule() 
  {
		LLVMContext &Context = getGlobalContext();
    Module* M;
    
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
 

CombinedProfile* getCP(const std::string& filename, Module& M)
{
  CombinedProfile* rc = NULL;
  CPFactory fact = CPFactory(M);

  if( !fact.buildProfiles(InputFilenames[0]) )
  {
    errs() << "Failed to read profile\n";
    return(rc);
  }
  
  int numProfs = fact.hasEdgeCP() + fact.hasPathCP() + fact.hasCallCP();
  if(numProfs != 1)
  {
    errs() << "Error: CP file has more than one type of profile\n";
    return(rc);
  }
  
  if(fact.hasEdgeCP()) rc = fact.takeEdgeCP();
  if(fact.hasPathCP()) rc = fact.takePathCP();
  if(fact.hasCallCP()) rc = fact.takeCallCP();
  
  return(rc);
}

int main(int argc, char *argv[]) 
{
	CombinedProfile *cp1 = NULL;
  CombinedProfile *cp2 = NULL;

  // Print a stack trace if we signal out.
	sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

	// Call llvm_shutdown() on exit.
  llvm_shutdown_obj Y;

	// Setup command line arguments
  cl::ParseCommandLineOptions(argc, argv,
		"llvm combined edge/path profile analyzer\n");

  if( !Summary && !Stats && !Drift && !Print )
  {
    // no output selected, set default
    Summary = true;
  }

  // Get access to the current module
  Module* currentModule = loadModule();
  if( currentModule == NULL ) return 1;

  CPFactory fact = CPFactory(*currentModule); 

  // check number of input files and load the profiles
  if(Drift)
  {
    if(InputFilenames.size() != 2)
    {
      errs() << "error: need 2 CPs to compute drift\n";
      return(1);
    }
        
    cp1 = getCP(InputFilenames[0], *currentModule);
    cp2 = getCP(InputFilenames[1], *currentModule);

    if( (cp1 == NULL) || (cp2 == NULL) )
    {
      errs() << "Failed to load two profiles\n";
      return(-1);
    }
    if( cp1->getProfilingType() != cp1->getProfilingType() )
    {
      errs() << "Profiles are not of the same type\n";
      return(-1);
    }

  }
  else
  { 
    if(InputFilenames.size() != 1)
    {
      errs() << "error: can only print info for 1 CP\n";
      return(1);
    }    
    cp1 = getCP(InputFilenames[0], *currentModule);
  }


  //
  // Summary
  //
  if(Summary)
  {
    VERBOSE(errs() << "Printing Summary:\n");
    cp1->printSummary(outs());
    VERBOSE(errs() << "end summary\n");
  }

  //
  // Stats
  //
  if(Stats)
  {
    VERBOSE(errs() << "Printing Stats:\n");
    cp1->printHistogramStats(outs());
    VERBOSE(errs() << "end stats\n");
  }

  //
  // Print
  //
  if(Print)
  {
    VERBOSE(errs() << "Printing:\n");
    cp1->print(outs());
    VERBOSE(errs() << "end print\n");
  }

  //
  // Drift
  //
  if(Drift)
  {
    VERBOSE(errs() << "Printing Drift:\n");
    cp1->printDrift(*cp2, outs());
    VERBOSE(errs() << "end drift\n");
  }

  //errs() << "Cleaning up...\n";

  // clean up loaded module
  if (currentModule)
    delete currentModule;

  if(cp1 != NULL) delete cp1;
  if(cp2 != NULL) delete cp2;

  fact.freeStaticData();

  //errs() << "Done Cleanup\n";

  return(0);

}
