//===- EdgeNumberPrinter.cpp ----------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Output CFG .dot files of each function showing edge profiling numbers
//
//===----------------------------------------------------------------------===//
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
  class GenerateEdgeNumberCFGsPass: public ModulePass {
  private:
    bool runOnModule(Module &M);

  public:
    static char ID; // Pass identification, replacement for typeid
    GenerateEdgeNumberCFGsPass() : ModulePass(ID) {}
  };
}

char GenerateEdgeNumberCFGsPass::ID = 0;
static RegisterPass<GenerateEdgeNumberCFGsPass> X("dot-edge-numbers",
    "Print each functions' CFG along with its edge numbers to a 'dot' file.");

ModulePass *llvm::createGenerateEdgeNumberCFGsPass() {
  return new GenerateEdgeNumberCFGsPass();
}

bool GenerateEdgeNumberCFGsPass::runOnModule (Module &M) {
	unsigned edgeCounter = 0;

	for (Module::iterator F = M.begin(), E = M.end(); F != E; F++ ) {
		if( F->isDeclaration() )
			continue;

		std::string errorInfo;
		std::string functionName = F->getNameStr();
		std::string filename = "edgenum." + functionName + ".dot";
		unsigned startingEdge = edgeCounter;

		errs() << "Writing '" << filename << "'...\n";

		raw_fd_ostream dotFile(filename.c_str(), errorInfo);

		if (!errorInfo.empty()) {
			errs() << "Error opening '" << filename.c_str() <<"' for writing!";
			errs() << "\n";
			continue;
		}

		dotFile << "digraph " << functionName << " {\n";
		dotFile << "\t\"(null)\" -> \"" << F->getEntryBlock().getNameStr()
			<< "\" [label=" << edgeCounter++ << "]\n";

		for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
			TerminatorInst *TI = BB->getTerminator();

			for (unsigned s = 0, e = TI->getNumSuccessors(); s != e; ++s)
				dotFile << "\t\"" << BB->getNameStr() << "\" -> \""
					<< TI->getSuccessor(s)->getNameStr() << "\" [label=" << edgeCounter++ << "]\n";
		}

		dotFile << "\tlabel=\"" << functionName << ": " << startingEdge
			<< " - " << edgeCounter - 1 << "\"\n}\n";
	}

	return true;
}
