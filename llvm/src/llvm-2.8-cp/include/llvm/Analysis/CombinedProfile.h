//===- CombinedProfile.h --------------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Definitions nescessary for loading/merging combined profiles.
//
//===----------------------------------------------------------------------===//

#ifndef COMBINEDPROFILE_H
#define COMBINEDPROFILE_H

#include <stdio.h>

#include "llvm/Analysis/ProfileInfoTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"  // CallSites are value classes
#include <vector>
#include <list>
#include <map>
#include <set>
#include "llvm/Analysis/CPHistogram.h"

#define DEFAULT_BINS 20

namespace llvm {
  class Module;
  class Function;
  class EdgeDominatorTree;
	class CombinedProfile;
	class CombinedEdgeProfile;
	class CombinedPathProfile;
  class CombinedCallProfile;

	// --------------------------------------------------------------------------
	// CombinedProfile - Implements a set of common functions and variables used
	//                   by combined edge and combined path profiling.
	// --------------------------------------------------------------------------

	typedef std::vector<CPHistogram*> CPHistVec;

  typedef std::set<unsigned> IndexSet;

	typedef std::list<CombinedProfile*> CPList;

  class CombinedProfile {
  public:
		CombinedProfile();
    virtual ~CombinedProfile();

    // getNameStr allows printing methods to print the type of CP
    virtual const std::string& getNameStr() const 
    {
      static const std::string type="base";
      return(type);
    };

    virtual ProfilingType getProfilingType() const = 0;

    // read in a raw profile from the file
    virtual bool addProfile(FILE* file) = 0;
		virtual unsigned serialize(FILE* f) = 0;
		virtual bool deserialize(FILE* f) = 0;

    // call buildFromList on every histogram
		void buildHistograms(unsigned binCount);
    virtual bool buildFromList(CPList& list, unsigned bincount = 0) = 0;

    // print various info
    void print(llvm::raw_ostream& stream);
    void printHistogramInfo(llvm::raw_ostream& stream);
    void printHistogramStats(llvm::raw_ostream& stream);
    void printSummary(llvm::raw_ostream& stream);
    void printDrift(const CombinedProfile& other, 
                    llvm::raw_ostream& stream) const;

		unsigned getBinCount() const;
    unsigned calcBinCount(CPList& list, unsigned fallback = DEFAULT_BINS) const;
		double getTotalWeight() const;
		void addWeight(double w = 1.0);

    unsigned size() const {return(_histograms.size());};
    CPHistVec::iterator begin() {return(_histograms.begin());};
    CPHistVec::iterator end() {return(_histograms.begin());};

    // Implement in all subclasses (statics can't be virtual):
    //static void freeStaticData() = 0;

	protected:
		double _weight;
		unsigned _bincount;
    // the actual histograms.  build an index map on top of
    // _histograms if you need a sparse/non-int mapping from ID-->histogram
    CPHistVec _histograms;  
  };  // class (virtual) CombinedProfile

  // --------------------------------------------------------------------------
  // Combined Edge Profile
  // --------------------------------------------------------------------------

  class CombinedEdgeProfile : public CombinedProfile {
	public:
    explicit CombinedEdgeProfile(Module& module);
    
    const std::string& getNameStr() const 
    {
      static const std::string type="edge";
      return(type);
    };

    ProfilingType getProfilingType() const {return(CombinedEdgeInfo);};

    bool addProfile(FILE* f);
		unsigned serialize(FILE* f);
		bool deserialize(FILE* f);
    
    //static unsigned calcBinCount(CEPList& list, 
    //                             unsigned fallback = DEFAULT_BINS);
    //bool buildFromList(CEPList& hl, unsigned binCount = 0);
    bool buildFromList(CPList& list, unsigned binCount = 0);
    
    CPHistogram* operator[](const int index);

    static void freeStaticData();

	private:
    static EdgeDominatorTree* _edt;
  };  // class CombinedEdgeProfile


  // --------------------------------------------------------------------------
  // Combined Path Profile
  // --------------------------------------------------------------------------

  // these give semantics to the template parameters
  typedef unsigned PathIndex;
  typedef unsigned FunctionIndex;
  typedef std::pair<FunctionIndex,PathIndex> PathID;

  typedef std::set<PathID> PathSet;

  // PathIndex --> index in _histograms
  typedef std::map<PathIndex,unsigned> CPPHistogramMap;

  typedef std::map<FunctionIndex,CPPHistogramMap> CPPFunctionMap;

	class CombinedPathProfile : public CombinedProfile {
	public:
		explicit CombinedPathProfile(Module& module);

    const std::string& getNameStr() const 
    {
      static const std::string type="path";
      return(type);
    };

    ProfilingType getProfilingType() const {return(CombinedPathInfo);};

    bool addProfile(FILE* f);
		unsigned serialize(FILE* f);
		bool deserialize(FILE* f);

    //static unsigned calcBinCount(CPPList& list, 
    //                             unsigned fallback = DEFAULT_BINS);
		bool buildFromList(CPList& list, unsigned binCount);

    // override printDrift because histogram indexes are not
    // consistent across path profiles
    void printDrift(CombinedPathProfile& other, 
                    llvm::raw_ostream& stream);

		unsigned getFunctionCount() const;
    bool valid(const PathID& path) const;
    CPHistogram& getHistogram(const FunctionIndex funcIndex, 
                              const PathIndex pathIndex);
    CPHistogram& getHistogram(const PathID& path);
    CPHistogram& operator[](const PathID& path);

    void getPathSet(PathSet& paths) const;

    static void freeStaticData() {};

	private:
    //_functions can't be static because mapping is not consistent
		CPPFunctionMap _functions; // sparse map <funcID,pathID> --> histogram index
    std::vector<Function*> _functionRef;
  }; // class CombinedPathProfile


  // --------------------------------------------------------------------------
  // Combined Call Profile
  // --------------------------------------------------------------------------

  typedef unsigned CallIndex;
  typedef std::set<CallIndex> CallSet;

	//typedef std::list<CombinedCallProfile*> CCPList;

  typedef std::map<BasicBlock*,unsigned> CallProfileMap;
  typedef std::map<Function*,unsigned> FunctionFreqMap;

  typedef std::vector<unsigned> UnsignedVec;
  typedef std::vector<Function*> FunctionVec;

  class CombinedCallProfile : public CombinedProfile {
	public:
    explicit CombinedCallProfile(Module& M);
    
    const std::string& getNameStr() const 
    {
      static const std::string type="call";
      return(type);
    };

    ProfilingType getProfilingType() const {return(CombinedCallInfo);};

    unsigned serialize(FILE* f);
    bool deserialize(FILE* f);

    bool addProfile(FILE* f);

    //static unsigned calcBinCount(CCPList& list, 
    //                             unsigned fallback = DEFAULT_BINS);
    bool buildFromList(CPList& list, unsigned binCount);
    
    bool hasCall(BasicBlock* BB);
    bool isEntry(BasicBlock* BB);

    CPHistogram& operator[](const CallIndex index);
    CPHistogram& operator[](BasicBlock* bb);

    // !! void getCallSet(CallSet& calls) const;

    bool isFDOInliningCandidate(Instruction* I);
    bool hasFDOInliningCandidate(BasicBlock* BB);

    static void freeStaticData() { _profmap.clear(); _funcIndex.clear(); 
      _funcRef.clear(); _entryCalls.clear(); _histCnt = 0; }

	private:
    // Program structure mappings only need to be computed once!
    static CallProfileMap _profmap;  // instr. BB --> index in _histograms
    static UnsignedVec _funcIndex;   // instr. BB index --> function index
    static FunctionVec _funcRef;     // function index --> function
    static UnsignedVec _entryCalls;  // counter indexes of entry BBs w/ calls
    static unsigned _histCnt;        // number of histograms
    UnsignedVec _funcFreq;    // function index --> entry frequency

    // Use CS.getParent() to get BB; look up profile in _profmap.

  };  // class CombinedCallProfile


}  // namespace llvm

#endif
