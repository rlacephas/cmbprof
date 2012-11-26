//===- PathProfileInfo.cpp ------------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface used by optimizers to load path profiles,
// and provides a loader pass which reads a path profile file.
//
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "path-profile-info"

#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ProfileInfoTypes.h"
#include "llvm/Analysis/PathProfileInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>

using namespace llvm;

// command line option for loading path profiles
static cl::opt<std::string>
PathProfileInfoFilename("path-profile-loader-file", cl::init("llvmprof.out"),
                        cl::value_desc("filename"),
                        cl::desc("Path profile file loaded by -path-profile-loader"));

namespace {
  class PathProfileLoaderPass : public ModulePass, public PathProfileInfo {
  public:
    PathProfileLoaderPass() : ModulePass(ID) { }
    ~PathProfileLoaderPass();

    // this pass doesn't change anything (only loads information)
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    }

    // the full name of the loader pass
    virtual const char* getPassName() const {
      return "Path Profiling Information Loader";
    }

    // required since this pass implements multiple inheritance
		virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
      if (PI == &PathProfileInfo::ID)
        return (PathProfileInfo*)this;
      return this;
    }

    // entry point to run the pass
    bool runOnModule(Module &M);

    // pass identification
    static char ID;

  private:
    // make a reference table to refer to function by number
    void buildFunctionRefs(Module &M);

    // process argument info of a program from the input file
    void handleArgumentInfo();

    // process path number information from the input file
    void handlePathInfo();

    // array of references to the functions in the module
    std::vector<Function*> _functions;

    // path profile file handle
    FILE* _file;

    // path profile file name
    std::string _filename;
  };
}

// register PathLoader
char PathProfileLoaderPass::ID = 0;
static RegisterPass<PathProfileLoaderPass> X("path-profile-loader",
  "Load path profile information from file");

// setup PathProfileInfo as an analysis group
static RegisterAnalysisGroup<PathProfileInfo> Y(X);
const PassInfo *llvm::PathProfileLoaderPassID = &X;

// link PathLoader as a pass, and make it available as an optimisation
ModulePass *llvm::createPathProfileLoaderPass() {
  return new PathProfileLoaderPass;
}

// ----------------------------------------------------------------------------
// PathEdge implementation
//
PathEdge::PathEdge (BasicBlock* source, BasicBlock* target, unsigned duplicateNumber) :
	_source(source), _target(target), _duplicateNumber(duplicateNumber) {
}

unsigned PathEdge::getDuplicateNumber() {
	return _duplicateNumber;
}

BasicBlock* PathEdge::getSource() {
	return _source;
}

BasicBlock* PathEdge::getTarget() {
	return _target;
}

// ----------------------------------------------------------------------------
// Path implementation
//

Path::Path (unsigned int number, unsigned int count, double countStdDev, PathProfileInfo* ppi) :
  _number(number) , _count(count), _countStdDev(countStdDev), _ppi(ppi) {
}

double Path::getFrequency() const {
  return 100 * double(_count) /
    double(_ppi->_functionPathCounts[_ppi->_currentFunction]);
}

unsigned int Path::getNumber() const {
  return _number;
}

unsigned int Path::getCount() const {
  return _count;
}

double Path::getCountStdDev() const {
	return _countStdDev;
}

static BallLarusEdge* getNextEdge (BallLarusNode* node,
    unsigned int pathNumber) {
  BallLarusEdge* best = 0;

  for( BLEdgeIterator next = node->succBegin(),
      end = node->succEnd(); next != end; next++ ) {
    if( (*next)->getType() != BallLarusEdge::BACKEDGE && // we don't want backedges
			  (*next)->getType() != BallLarusEdge::SPLITEDGE && // we don't want split edges
        (*next)->getWeight() <= pathNumber && // weight must be <= pathNumber
        (!best || (best->getWeight() < (*next)->getWeight())) ) // best one?
      best = *next;
  }

  return best;
}

PathEdgeVector* Path::getPathEdges() const {
  BallLarusNode* currentNode = _ppi->_currentDag->getRoot ();
  unsigned int increment = _number;
  PathEdgeVector* pev = new PathEdgeVector;

  while (currentNode != _ppi->_currentDag->getExit()) {
    BallLarusEdge* next = getNextEdge(currentNode, increment);

    increment -= next->getWeight();

    if( next->getType() != BallLarusEdge::BACKEDGE_PHONY &&
				next->getType() != BallLarusEdge::SPLITEDGE_PHONY &&
				next->getTarget() != _ppi->_currentDag->getExit() )
				pev->push_back(PathEdge(
			next->getSource()->getBlock(), next->getTarget()->getBlock(), next->getDuplicateNumber()));

		if( next->getType() == BallLarusEdge::BACKEDGE_PHONY &&
			next->getTarget() == _ppi->_currentDag->getExit() )
			pev->push_back(PathEdge(
				next->getRealEdge()->getSource()->getBlock(),
				next->getRealEdge()->getTarget()->getBlock(),
				next->getDuplicateNumber()));

		if( next->getType() == BallLarusEdge::SPLITEDGE_PHONY &&
				next->getSource() == _ppi->_currentDag->getRoot() )
			pev->push_back(PathEdge(
				next->getRealEdge()->getSource()->getBlock(),
				next->getRealEdge()->getTarget()->getBlock(),
				next->getDuplicateNumber()));

		// set the new node
    currentNode = next->getTarget();
  }

  return pev;
}

PathBlockVector* Path::getPathBlocks() const {
  BallLarusNode* currentNode = _ppi->_currentDag->getRoot ();
  unsigned int increment = _number;
  PathBlockVector* pbv = new PathBlockVector;

  while (currentNode != _ppi->_currentDag->getExit()) {
    BallLarusEdge* next = getNextEdge(currentNode, increment);
    increment -= next->getWeight();

    // add block to the block list if it is a real edge
    if( next->getType() == BallLarusEdge::NORMAL)
      pbv->push_back (currentNode->getBlock());
    // make the back edge the last edge since we are at the end
    else if( next->getTarget() == _ppi->_currentDag->getExit() ) {
      pbv->push_back (currentNode->getBlock());
      pbv->push_back (next->getRealEdge()->getTarget()->getBlock());
    }

    // set the new node
    currentNode = next->getTarget();
  }

  return pbv;
}

BasicBlock* Path::getFirstBlockInPath() const {
	BallLarusNode* root = _ppi->_currentDag->getRoot();
	BallLarusEdge* edge = getNextEdge(root, _number);

	if( edge && (edge->getType() == BallLarusEdge::BACKEDGE_PHONY ||
			edge->getType() == BallLarusEdge::SPLITEDGE_PHONY) )
		return edge->getTarget()->getBlock();

	return root->getBlock();
}

// ----------------------------------------------------------------------------
// PathProfileInfo implementation
//

// Pass identification
char llvm::PathProfileInfo::ID = 0;

PathProfileInfo::PathProfileInfo () : _currentDag(0) , _currentFunction(0) {
}

PathProfileInfo::~PathProfileInfo() {
	if (_currentDag)
    delete _currentDag;
}

// set the function for which paths are currently begin processed
void PathProfileInfo::setCurrentFunction(Function* F) {
  // Make sure it exists
  if (!F) return;

  if (_currentDag)
    delete _currentDag;

  _currentFunction = F;
  _currentDag = new BallLarusDag(*F);
  _currentDag->init();
  _currentDag->calculatePathNumbers();
}

// get the function for which paths are currently being processed
Function* PathProfileInfo::getCurrentFunction() const {
  return _currentFunction;
}

// get the entry block of the function
BasicBlock* PathProfileInfo::getCurrentFunctionEntry() {
	return _currentDag->getRoot()->getBlock();
}

// return the path based on its number
Path* PathProfileInfo::getPath(unsigned int number) {
  return _functionPaths[_currentFunction][number];
}

// return the number of paths which a function may potentially execute
unsigned int PathProfileInfo::getPotentialPathCount() {
  return _currentDag ? _currentDag->getNumberOfPaths() : 0;
}

// return an iterator for the beginning of a functions executed paths
PathIterator PathProfileInfo::pathBegin() {
  return _functionPaths[_currentFunction].begin();
}

// return an iterator for the end of a functions executed paths
PathIterator PathProfileInfo::pathEnd() {
  return _functionPaths[_currentFunction].end();
}

// returns the total number of paths run in the function
unsigned int PathProfileInfo::pathsRun() {
		return _currentFunction ? _functionPaths[_currentFunction].size() : 0;
}

// ----------------------------------------------------------------------------
// PathLoader implementation
//

// remove all generated paths
PathProfileLoaderPass::~PathProfileLoaderPass() {
  for( FunctionPathIterator funcNext = _functionPaths.begin(),
      funcEnd = _functionPaths.end(); funcNext != funcEnd; funcNext++)
    for( PathIterator pathNext = funcNext->second.begin(),
        pathEnd = funcNext->second.end(); pathNext != pathEnd; pathNext++)
      delete pathNext->second;
}

// entry point of the pass; this loads and parses a file
bool PathProfileLoaderPass::runOnModule(Module &M) {
  // get the filename and setup the module's function references
  _filename = PathProfileInfoFilename;
  buildFunctionRefs (M);

  if (!(_file = fopen(_filename.c_str(), "rb"))) {
		errs () << "error: input '" << _filename << "' file does not exist.\n";
		return false;
	}

  ProfilingType profType;

  while( fread(&profType, sizeof(ProfilingType), 1, _file) ) {
    switch (profType) {
    case ArgumentInfo:
      handleArgumentInfo ();
      break;
    case PathInfo:
      handlePathInfo ();
      break;
    default:
      errs () << "error: bad path profiling file syntax\n";
      fclose (_file);
      return false;
    }
  }

	fclose (_file);

  return true;
}

// create a reference table for functions defined in the path profile file
void PathProfileLoaderPass::buildFunctionRefs (Module &M) {
  _functions.push_back(0); // make the 0 index a null pointer

  for (Module::iterator F = M.begin(), E = M.end(); F != E; F++) {
    if (F->isDeclaration())
      continue;
    _functions.push_back(F);
  }
}

// handle command like argument infor in the output file
void PathProfileLoaderPass::handleArgumentInfo() {
  // get the argument list's length
  unsigned savedArgsLength;
	if( fread(&savedArgsLength, sizeof(unsigned), 1, _file) != 1 ) {
		errs() << "warning: argument info header/data mismatch\n";
		return;
	}

  // allocate a buffer, and get the arguments
  char* args = new char[savedArgsLength+1];
  if( fread(args, 1, savedArgsLength, _file) != savedArgsLength )
    errs() << "warning: argument info header/data mismatch\n";

	args[savedArgsLength] = '\0';
	argList = std::string(args);
  delete [] args; // cleanup dynamic string

  // byte alignment
	fseek(_file, (4-(savedArgsLength&3))%4, SEEK_CUR);
}

// Handle path profile information in the output file
void PathProfileLoaderPass::handlePathInfo () {
  // get the number of functions in this profile
	unsigned functionCount;
	if( fread(&functionCount, sizeof(functionCount), 1, _file) != 1 ) {
		errs() << "warning: path info header/data mismatch\n";
		return;
	}

  // gather path information for each function
	for (unsigned i = 0; i < functionCount; i++) {
    PathHeader pathHeader;
    if( fread(&pathHeader, sizeof(pathHeader), 1, _file) != 1 ) {
      errs() << "warning: bad header for path function info\n";
			break;
    }

    Function* f = _functions[pathHeader.fnNumber];

    // dynamically allocate a table to store path numbers
    PathTableEntry* pathTable = new PathTableEntry[pathHeader.numEntries];

    if( fread(pathTable, sizeof(PathTableEntry), pathHeader.numEntries, _file)
      != pathHeader.numEntries) {
        delete [] pathTable;
        errs() << "warning: path function info header/data mismatch\n";
        return;
    }

    // Build a new path for the current function
    unsigned int totalPaths = 0;
    for (unsigned int j = 0; j < pathHeader.numEntries; j++) {
      totalPaths += pathTable[j].pathCounter;
      _functionPaths[f][pathTable[j].pathNumber]
        = new Path(pathTable[j].pathNumber, pathTable[j].pathCounter, 0, this);
    }

    _functionPathCounts[f] = totalPaths;

    delete [] pathTable;
	}
}
