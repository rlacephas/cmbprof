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
#include "llvm/Analysis/PathNumbering.h"
#include "llvm/Analysis/EdgeDominatorTree.h"
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

	// Combined edge profiling cumulative storage file
	cl::opt<std::string> CPEdgeFile("eo",
		cl::init("edge.cp"), cl::value_desc("filename"),
		cl::desc("Combined edge profiling cumulative storage file."));

	// Combined path profiling cumulative storage file
	cl::opt<std::string> CPPathFile("po",
		cl::init("path.cp"), cl::value_desc("filename"),
		cl::desc("Combined path profiling cumulative storage file."));

	// Number of bins that output histograms will have
	cl::opt<unsigned> CPBinCount("bc",
		cl::init(15), cl::value_desc("count"),
		cl::desc("Combined profiling bin count."));

	// print a selected histogram
	cl::opt<unsigned> PrintEdgeHist("peh",
		cl::init(0), cl::value_desc("edgeID"),
		cl::desc("Print selected edge histogram"));

	// print a selected histogram
	cl::opt<unsigned> queryEdge("qe",
		cl::init(0), cl::value_desc("edgeID"),
		cl::desc("Inspect this edge."));

	// Verbose: output specifics regarding which edges/ paths are merged
	cl::opt<bool>
		Verbose("v",
		cl::desc("Output specifics about which edges/paths are merged."));

	// Print bin usage statistics
	cl::opt<bool>
		BinsUsed("binuse",
		cl::desc("Details of histogram bin usage."));

	cl::opt<bool>
		PrintEdgeHistogram("pe",
		cl::desc("Print out a text representation of each histogram in "
		"the CEP output file"));

	cl::opt<bool>
		PrintPathHistogram("pp",
		cl::desc("Print out a text representation of each histogram in "
		"the CPP output file"));

	// Profiling files to be merged into the "master" combined profiling files
	cl::list<std::string> InputFilenames(cl::Positional, cl::OneOrMore,
		cl::desc("<input edge/path files>"));

	// Handle to the current module
	Module* currentModule = 0;
  std::vector<Function*> functionRef;
	EdgeDominatorTree* edt = 0;

  // ---------------------------------------------------------------------------
 
  // load a module's bitcode into memory
	bool loadModule() {
		LLVMContext &Context = getGlobalContext();

		// Read in the bitcode file ...
		std::string ErrorMessage;
		if (MemoryBuffer *Buffer = MemoryBuffer::getFileOrSTDIN(BitcodeFile,
				&ErrorMessage)) {
			currentModule = ParseBitcodeFile(Buffer, Context, &ErrorMessage);
			delete Buffer;
		}

		// Ensure the module has been loaded
		if (!currentModule) {
			errs() << BitcodeFile << ": " << ErrorMessage << "\n";
			return false;
		}

		// Get function count of module
		for( Module::iterator F = currentModule->begin(), E = currentModule->end();
				F != E; F++ )
			if( !F->isDeclaration() )
				functionRef.push_back(F);

		return true;
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


  // get the first BallLarusEdge
  BallLarusEdge* getFirstBLEdge (BallLarusNode* node, unsigned int pathNumber) {
    BallLarusEdge* best = 0;

    for( BLEdgeIterator next = node->succBegin(),
        end = node->succEnd(); next != end; next++ ) {
      if( (*next)->getWeight() <= pathNumber && // weight must be <= pathNumber
          (!best || (best->getWeight() < (*next)->getWeight())) ) // best one?
        best = *next;
    }

    return best;
  }

  /*
  // process info related to a trial's path profiling
	void readPathInfo(FILE* file) {
		// get the number of functions in this profile
		unsigned functionCount;
		if( fread(&functionCount, sizeof(unsigned), 1, file) != 1 ) {
			errs() << "  error: path profiling info has no header\n";
			return;
		}

		VERBOSE(errs() << "  " << functionCount << " path function(s) identified.\n");

		// If this is the first profile added, allocate newCPPFromSingles
		if( !newCPPFromSingles )
			newCPPFromSingles = new CombinedPathProfile;

		newCPPFromSingles->incTrialCount();

		// Iterate through each function
		for( unsigned i = 0; i < functionCount; i++ ) {
			PathHeader functionHeader;
			if( fread(&functionHeader, sizeof(PathHeader), 1, file) != 1 ) {
				errs() << "  error: bad path profiling file syntax\n";
				return;
			}

			// Build a DAG for the function
			BallLarusDag dag(*functionRef[functionHeader.fnNumber-1]);
			dag.init();
			dag.calculatePathNumbers();

			newCPPFromSingles->setCurrentFunction(functionHeader.fnNumber);
			unsigned totalNumberExecuted = 0;
			std::list<PathTableEntry> newPaths;

			// Iterate through each path entry, and add it
			for( unsigned ii = 0; ii < functionHeader.numEntries; ii++ ) {
				PathTableEntry pte;
				if( fread(&pte, sizeof(PathTableEntry), 1, file) != 1 ) {
					errs() << "  error: bad path profiling file syntax\n";
					return;
				}

				newPaths.push_back(pte);
				if( getFirstBLEdge(dag.getRoot(),pte.pathNumber)->getType() ==
						BallLarusEdge::NORMAL && totalNumberExecuted < 0xffffffff ) {
					//errs() << "Path #" << pte.pathNumber << " is normal!\n";
					totalNumberExecuted += pte.pathCounter;
				}
			}

			for( std::list<PathTableEntry>::iterator P = newPaths.begin(),
					E = newPaths.end(); P != E; P++ )
				if( P->pathCounter )
					(*newCPPFromSingles)[P->pathNumber].AddNewFreq(double(P->pathCounter)/totalNumberExecuted);
		}

	}
  */

	// deserialize and return a new CEP
  // return NULL on failure
  CombinedEdgeProfile* readCombinedEdgeInfo(FILE* file) {
    errs() << "--> readCombinedEdgeInfo\n";
  	CombinedEdgeProfile* cp = new CombinedEdgeProfile(edt->getEdgeCount());

  	if( !cp->BuildFromFile(file) )
    {
      delete cp;
      errs() << "<-- readCombinedEdgeInfo (fail)\n";
			return(NULL);
    }

    errs() << "<-- readCombinedEdgeInfo\n";
    return(cp);
  }

  /*
	// deserialize and return a new CPP
  // return NULL on failure
	CombinedPathProfile* readCombinedPathInfo(FILE* file) {
  	CombinedPathProfile* cp = new CombinedPathProfile;
    errs() << "--> readCombinedPathInfo\n";
  	if( !cp->BuildFromFile(file) )
    {
      delete cp;
      errs() << "<-- readCombinedPathInfo (fail)\n";
			return(NULL);
    }

    errs() << "<-- readCombinedPathInfo\n";
    return(cp);
  }
  */

  // process an input file which may contain a series of trials
  // CP-from-singles are item 0 (front()) in their respective lists.
	void processFile(FILE* file, CEPList* cepList, CPPList* cppList) {
		ProfilingType profType;
		bool readEdge = false;
		bool readPath = false;
    CombinedEdgeProfile* cep = NULL;
    //CombinedPathProfile* cpp = NULL;

    //errs() << "--> processFile\n";

		// So long as there is an available header, read it and process it
		while( fread(&profType, sizeof(ProfilingType), 1, file) > 0) {
			// What to do with this specific profiling type
			switch (profType) {
			// This marks the beginning of a new trial
			case ArgumentInfo:
				readArgumentInfo (file);
				readEdge = false;
				readPath = false;
				break;

			// Found edge profile, so process and add it
			case EdgeInfo:
				// make sure there is only 1 edge profile per trial
				if( readEdge ) {
					errs() << "  error: multiple edge profiles per trial.\n";
					return;
				}
				VERBOSE(errs() << "  reading edge profile info.\n");
				readEdge = true;
        cepList->front()->addEdgeProfile(CPBinCount, edt, file);
				//readEdgeInfo(file);
				break;

			// Found path profile, so process and add it
        /*
			case PathInfo:
				// make sure there is only 1 path profile per trial
				if( readPath ) {
					errs() << "  error: multiple path profiles per trial.\n";
					return;
				}
				VERBOSE(errs() << "  reading path profile info.\n");
				readPath = true;
				cpp->front()->addPathProfile(CPBinCount, file);
        //readPathInfo(file);
				break;
        */

			// Found an edge histogram
			case CombinedEdgeInfo:
				VERBOSE(errs() << "  reading combined edge profile info.\n");
				cep = readCombinedEdgeInfo(file);
        if( cep != NULL)
          cepList->push_back(cep);
				break;

			// Found a path histogram
        /*
			case CombinedPathInfo:
				VERBOSE(errs() << "  reading combined path profile info.\n");
				cpp = readCombinedPathInfo(file);
        if(cpp != NULL)
          cppList->push_back(cpp);
				break;
        */

			// non-handled profiling type
			default:
				errs () << "  error: bad profiling file header\n";
				return;
			}
		}
    //errs() << "<-- processFile\n";
	}
}

int main(int argc, char *argv[]) {
	// Lists of combined profiles which need to be merged
	CEPList newCEPs;
	CPPList newCPPs;

  CombinedEdgeProfile* cepOut = NULL;

  // Print a stack trace if we signal out.
	sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

	// Call llvm_shutdown() on exit.
  llvm_shutdown_obj Y;

	// Setup command line arguments
  cl::ParseCommandLineOptions(argc, argv,
		"llvm combined edge/path profile merger\n");

	// Get access to the current module
	if( !loadModule() ) return 1;

	// Build EDT
	edt = new EdgeDominatorTree(*currentModule);
  unsigned initEC = edt->getEdgeCount();
  unsigned initDom = 0;
  if(queryEdge > 0) initDom = edt->getDominatorIndex(queryEdge);

  //errs() << "EDT edge count: " << edt->getEdgeCount() << "\n";


  // Put the from-singles CPs into the lists
  newCEPs.push_back(new CombinedEdgeProfile(0));
  //newCPPs.push_back(new CombinedPathProfile);

  // Iterate through each of the input files
  bool error = false;
	for( unsigned i = 0; i < InputFilenames.size(); i++ ) {
		VERBOSE(errs() << "Processing '" << InputFilenames[i] << "' ...\n");

		// Open a handle to the profiling input file
		FILE* file = fopen(InputFilenames[i].c_str(),"rb");
		if (!file) {
			errs() << "  error: cannot open '" << InputFilenames[i] << "'\n";
      error = true;
      break;
		}

		// process data from the file
		processFile(file, &newCEPs, &newCPPs);

		fclose(file);
	}

  if(!error)
  {
    errs() << "Combine singles: " << newCEPs.front()->getTrialCount() << "\n";
    // build a potential CEP from singles list
    if(newCEPs.front()->getTrialCount() > 0)
      newCEPs.front()->BuildHistogramsFromAddList(CPBinCount);
    else
    {
      // no singles: remove from list and deallocate
      CombinedEdgeProfile* cep = newCEPs.front();
      newCEPs.pop_front();
      delete cep;
    }

    // build a potential CPP from singles list
    /*
      if( newCPPFromSingles ) {
			VERBOSE(errs() << "Building CPP histogram from single inputs:\n");
			newCPPFromSingles->BuildHistogramsFromAddList(CPBinCount);
			newCPPs.push_back(newCPPFromSingles);
      }
    */

    VERBOSE(errs() << "Building the output histograms ...\n");
    cepOut = new CombinedEdgeProfile(0);
    //CombinedPathProfile cppOut;
    
    errs() << "Build from list\n";
    
    cepOut->BuildFromList(newCEPs, CPBinCount);
    //cppOut.BuildFromList(newCPPs, functionRef.size(), CPBinCount);
    
    errs() << " done build from list\n";
  }

  errs() << "Begin Cleanup...\n";

  // clean up loaded module
  if (currentModule)
    delete currentModule;


  unsigned endEC = edt->getEdgeCount();
  unsigned endDom = 0;
  if(queryEdge > 0) endDom = edt->getDominatorIndex(queryEdge);
	// Clean up EDT
	delete edt;

	// Iterate and delete all CPs
	for( CEPIterator CP = newCEPs.begin(),
			E = newCEPs.end(); CP != E; CP++ )
		delete *CP;
  /*
	for( CPPIterator CP = newCPPs.begin(),
			E = newCPPs.end(); CP != E; CP++ )
		delete *CP;
  */

  errs() << "Done Cleanup\n";


  if(!error)
  {
    errs() << "Printing histograms\n";
    // Print out the histograms if the client has requested it
    if( PrintEdgeHistogram )
      cepOut->PrintHistograms();
    //if( PrintPathHistogram )
    //	cppOut.PrintHistograms();

    if(PrintEdgeHist > 0)
      cepOut->PrintHistogram(PrintEdgeHist);
    
    // Open a file handle and write the output histogram
    if( cepOut->getHistogramCount() ) {
      FILE* file = fopen(CPEdgeFile.c_str(),"wb");
      if (!file) {
        errs() << "  error: cannot open '" << CPEdgeFile.c_str() << "' for writing.\n";
        return -1;
      }
      
      VERBOSE(errs() << "Writing combined edge profile file '" << CPEdgeFile.c_str() << "' ...\n");
      cepOut->WriteToFile(file);
      
      fclose(file);
    }
        
    // Open a file handle and write the output histogram
    /*
      if( cppOut.getFunctionCount() ) {
      FILE* file = fopen(CPPathFile.c_str(),"wb");
      if (!file) {
			errs() << "  error: cannot open '" << CPPathFile.c_str() << "' for writing.\n";
			return -1;
      }
      
      VERBOSE(errs() << "Writing combined edge profile file '" << CPPathFile.c_str() << "' ...\n");
      cppOut.WriteToFile(file);
      
      errs() << "closing file\n";
      
      fclose(file);
      }
    */
    
    // print histogram bin usage statistics
    if(BinsUsed)
    {
      if(cepOut != NULL)
      {
        unsigned binBytes = 0;
        unsigned headerBytes = 0;
        unsigned totalBytes = 0;
        unsigned totalBins = 0;
        unsigned hcnt = cepOut->getHistogramCount();
        for(unsigned i = 0; i < hcnt; i++)
        {
          unsigned bins = (*cepOut)[i]->getBinsUsed();
          outs() << i << "\t" << bins << "\n";
          totalBins += bins;
        }
        binBytes = totalBins * sizeof(CombinedProfHistogramBin);
        headerBytes = hcnt * sizeof(CombinedProfEntry);
        totalBytes = binBytes + headerBytes;
        outs() << "Total Bins Used: " << totalBins << "\n";
        outs() << "Total Bytes: " << headerBytes << " + " << binBytes 
               << " = " << totalBytes << "\n";
      }
    }
    errs() << "free cepOut\n";
    delete cepOut;
    errs() << "free OK\n";

  }

  errs() << "EDT Edge Counts: " << initEC << " --> " << endEC << "\n";
  if(queryEdge > 0) 
    errs() << "EDT Dom " << queryEdge << ": " << initDom 
           << " --> " << endDom << "\n";

  if(error)
  {
    errs() << "cprof: Done (failure)\n";
    return(-1);
  }
  else
  {    
    errs() << "cprof: Done (success)\n";
    return 0;
  }
}
