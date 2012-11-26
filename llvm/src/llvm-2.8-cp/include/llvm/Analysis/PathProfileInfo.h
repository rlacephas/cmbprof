//===- PathProfileInfo.h --------------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file outlines the interface used by optimizers to load path profiles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BALLLARUSPATHPROFILEINFO_H
#define LLVM_ANALYSIS_BALLLARUSPATHPROFILEINFO_H

#include <stack>
#include "llvm/BasicBlock.h"
#include "llvm/Analysis/PathNumbering.h"

namespace llvm {
	class Path;
  class PathEdge;
  class PathProfileInfo;

  typedef std::vector<PathEdge> PathEdgeVector;
  typedef std::vector<PathEdge>::iterator PathEdgeIterator;

  typedef std::vector<BasicBlock*> PathBlockVector;
  typedef std::vector<BasicBlock*>::iterator PathBlockIterator;

  typedef std::map<unsigned int,Path*> PathMap;
  typedef std::map<unsigned int,Path*>::iterator PathIterator;

  typedef std::map<Function*,unsigned int> FunctionPathCountMap;
  typedef std::map<Function*,PathMap> FunctionPathMap;
  typedef std::map<Function*,PathMap>::iterator FunctionPathIterator;

	class PathEdge {
	public:
		PathEdge(BasicBlock* source, BasicBlock* target, unsigned duplicateNumber);

		unsigned getDuplicateNumber();
		BasicBlock* getSource();
		BasicBlock* getTarget();

	protected:
		BasicBlock* _source;
		BasicBlock* _target;
		unsigned _duplicateNumber;
	};

  class Path {
  public:
    Path(unsigned int number, unsigned int count,
			double countStdDev, PathProfileInfo* ppi);

    double getFrequency() const;

    unsigned int getNumber() const;
    unsigned int getCount() const;
    double getCountStdDev() const;

    PathEdgeVector* getPathEdges() const;
    PathBlockVector* getPathBlocks() const;

    BasicBlock* getFirstBlockInPath() const;

  private:
    unsigned int _number;
    unsigned int _count;
    double _countStdDev;

    // double pointer back to the profiling info
    PathProfileInfo* _ppi;
  };

  // TODO: overload [] operator for getting path
  // Add: getFunctionCallCount()
  class PathProfileInfo {
  public:
    PathProfileInfo();
    ~PathProfileInfo();

    void setCurrentFunction(Function* F);
    Function* getCurrentFunction() const;
    BasicBlock* getCurrentFunctionEntry();

    Path* getPath(unsigned int number);
    unsigned int getPotentialPathCount();

    PathIterator pathBegin();
    PathIterator pathEnd();
		unsigned int pathsRun();

    static char ID; // Pass identification
		std::string argList;

  protected:
    FunctionPathMap _functionPaths;
    FunctionPathCountMap _functionPathCounts;

  private:
    BallLarusDag* _currentDag;
    Function* _currentFunction;

    friend class Path;
  };
}

#endif
