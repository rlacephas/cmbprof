//===- EdgeDominatorTree.cpp ----------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "generate-edge-dominance"

#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/Analysis/EdgeDominatorTree.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
  class GenerateEdgeDominancePass: public ModulePass {
  private:
    bool runOnModule(Module &M);

  public:
    static char ID; // Pass identification, replacement for typeid
    GenerateEdgeDominancePass() : ModulePass(ID) {}

		// This pass only generates the edge profiling file; changes nothing
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    }
  };
}


EdgeDominatorTree::EdgeDominatorTree(Module &M) 
{
	EdgeIndex edgeCounter = 0; // PB replace with _edges.size()?

  unsigned totalEdges = 0;
	for( Module::iterator F = M.begin(), E = M.end(); F != E; F++ ) 
  {
    CFGEdgeDomTree funcEDT(*F, edgeCounter);

    // claim EdgeNode*s from funcEDT: now it's our job to free them
    EdgeNodeMap* localEdges = funcEDT.claimEdgeMap((void*)this);
    if(localEdges->size() > 0)
    {
      //errs() << "EDT: " << F->getName().str() << ": " << localEdges->size() << " edges\n";
      totalEdges += localEdges->size();
      //printDominance(errs(), *localEdges);
      edgeCounter += localEdges->size();
      // this is inserting *pointers* to the EdgeNodes allocated by funcEDT
      _edges.insert(localEdges->begin(), localEdges->end());
    }
  }
  //errs() << "EDT: total edges: " << totalEdges << "\n";
}


EdgeDominatorTree::~EdgeDominatorTree() {
		for( EdgeNodeMapIterator M = _edges.begin(), E = _edges.end();
				M != E; M++ )
			delete M->second;
}


EdgeIndex EdgeDominatorTree::getDominatorIndex(EdgeIndex e) {
  return(_edges[e]->domIndex);
}


unsigned EdgeDominatorTree::getEdgeCount() {
	return _edges.size();
}

// Find the depth of e from the root of the dominator tree.  The root
// is depth 0.
unsigned EdgeDominatorTree::getDepth(EdgeIndex e) 
{
  unsigned depth = 0;
  unsigned oldDom = e;
  unsigned newDom = getDominatorIndex(e);

  // dom(e) == e for root
  while(oldDom != newDom)
  {
    depth++;
    oldDom = newDom;
    newDom = getDominatorIndex(oldDom);
  }

  return(depth);
}


void EdgeDominatorTree::printDominance(llvm::raw_ostream& stream, 
                                       EdgeNodeMap& edges)
{
  stream << "Dominance Relationships (" << edges.size() << " edges)\n";
  for(EdgeNodeMapIterator edge = edges.begin(), edgeEnd = edges.end();
      edge != edgeEnd; edge++)
  {
    stream << "  " << edge->second->domIndex << " idoms " << edge->first << "\n";
  }
}


static cl::opt<std::string>
EdgeDominanceFilename("edge-dominance-file",
	cl::init("edgedom.out"),
	cl::value_desc("filename"),
  cl::desc("Edge dominance file generated by -generate-edge-dominance"));

char GenerateEdgeDominancePass::ID = 0;
static RegisterPass<GenerateEdgeDominancePass> X("generate-edge-dominance",
    "Generate a file containing edge dominance information, used by statistical profiling.");

ModulePass *llvm::createGenerateEdgeDominancePass() {
  return new GenerateEdgeDominancePass();
}

bool GenerateEdgeDominancePass::runOnModule (Module &M) {
	EdgeDominatorTree edt(M);

	// Prepare the output file
	std::string errorInfo;
	std::string filename = EdgeDominanceFilename;
	raw_fd_ostream domFile(filename.c_str(), errorInfo);

	if (!errorInfo.empty()) {
		errs() << "Error opening '" << filename.c_str() <<"' for writing!";
		errs() << "\n";
		return false;
	}

	errs() << "Generating edge dominance file ...\n";

	// Write each dominator edge to a file
	for( unsigned i = 0; i < edt.getEdgeCount(); i++ ) {
		unsigned domIndex = edt.getDominatorIndex(i);
		domFile.write((char*)&domIndex, sizeof(unsigned));
	}

	return true;
}
