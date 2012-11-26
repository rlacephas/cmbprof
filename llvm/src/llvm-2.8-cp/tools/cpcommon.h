
// Common functions for CP tools

#ifndef __CPCOMMON_H__
#define __CPCOMMON_H__

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
//#include "llvm/Analysis/PathNumbering.h"
//#include "llvm/Analysis/CPHistogram.h"
//#include "llvm/Analysis/EdgeDominatorTree.h"
//#include "llvm/Analysis/CombinedProfile.h"
//#include "llvm/Bitcode/ReaderWriter.h"
//#include "llvm/Support/CommandLine.h"
//#include "llvm/Support/Format.h"
//#include "llvm/Support/ManagedStatic.h"
//#include "llvm/Support/MemoryBuffer.h"
//#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
//#include "llvm/System/Signals.h"

#define VERBOSE(s) if( Verbose ) { s; }

using namespace llvm;

namespace cproftools {
  
  // load a module's bitcode into memory
  bool loadModule() 
  {
    LLVMContext &Context = getGlobalContext();
    Module* module;
    
    // Read in the bitcode file ...
    std::string ErrorMessage;
    if (MemoryBuffer *Buffer = MemoryBuffer::getFileOrSTDIN(BitcodeFile,
                                                            &ErrorMessage)) 
    {
      module = ParseBitcodeFile(Buffer, Context, &ErrorMessage);
      delete Buffer;
    }
    
    // Ensure the module has been loaded
    if (!currentModule) {
      errs() << BitcodeFile << ": " << ErrorMessage << "\n";
      return false;
    }
        
    VERBOSE(errs() << "Finished processing bitcode\n");
    return module;
  }


  // process info related to a trial's command line arguments
	void readArgumentInfo(FILE *file) {
		// get the argument list's length
		unsigned savedArgsLength;
		if( fread(&savedArgsLength, sizeof(unsigned), 1, file) != 1 ) {
			errs() << "  warning: argument info header/data mismatch\n";
			return;
		}

		// allocate a buffer, and get the arguments
		char* args = new char[savedArgsLength+1];
		if( fread(args, 1, savedArgsLength, file) != savedArgsLength )
			errs() << "  warning: argument info header/data mismatch\n";

		args[savedArgsLength] = '\0';
		VERBOSE(errs() << "  '" << args << "'\n");
		delete [] args; // cleanup dynamic string

		// byte alignment
		fseek(file, (4-(savedArgsLength&3))%4, SEEK_CUR);
	}


} // namespace cproftools

#endif
