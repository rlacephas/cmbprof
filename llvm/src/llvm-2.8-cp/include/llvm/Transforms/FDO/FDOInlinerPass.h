//===- FDOInlinerPass.h - Feedback-directed inlining ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_TRANSFORMS_FDO_FDOINLINER_H
#define LLVM_TRANSFORMS_FDO_FDOINLINER_H

#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/FDO/CPCallRecord.h"
#include "llvm/Transforms/FDO/TStream.h"
#include <map>
#include <list>

namespace llvm {

  class Module;
  class CallGraph;
  class TargetData;


  typedef DenseMap<const ArrayType*, std::vector<AllocaInst*> > 
  InlinedArrayAllocasTy;

  typedef std::map<CallSite,CPCallRecord*> CallMap;
  typedef std::set<CallSite> CallerSet;
  typedef std::map<Function*,CallerSet> CallerMap;

  typedef std::map<Function*, InlinedArrayAllocasTy> AllocaMap;
  typedef std::map<Function*, InlineFunctionInfo> IFIMap;

  class FDOInliner : public ModulePass {
  public:
    static char ID;

    //FDOInliner(char& ID);
    FDOInliner();
    ~FDOInliner();
    //FDOInliner(char& ID, int Threshold);
    bool runOnModule(Module& M);
    void getAnalysisUsage(AnalysisUsage &Info) const;
    
    bool isFDOInliningCandidate(Instruction* I);
    bool hasFDOInliningCandidate(BasicBlock* BB);


  protected:
    llvm::raw_fd_ostream* initLog(TStream& ts, const std::string& suffix, 
                                  unsigned p = vl::log);

    TStream debug;
    llvm::raw_fd_ostream* debugFD;
    TStream count;
    llvm::raw_fd_ostream* countFD;
    TStream cseval;
    llvm::raw_fd_ostream* csevalFD;
    TStream dead;
    llvm::raw_fd_ostream* deadFD;
    TStream hashlog;
    llvm::raw_fd_ostream* hashFD;

    unsigned initialize(Module& M, CallGraph& CG, const TargetData* TD);

    void finalReport(Module& M);

    // On inlining CallSite A into CALLER:
    //   - Delete A from _candidates
    //   - Delete A from _records
    //   - Delete A from _callers[A.callee]
    //   - Update mval of all _callers[CALLER]
    //   - For every callsite Ax in A that gets inlined into CALLER:
    //     - Insert A into _inliningHistory[Ax] (prevent indirect recursion)
    //     - Create record for Ax and insert into _candidates/_ignore, _records
    //   - Re-sort _candidates
    
    // =====================
    // Candidates
    // =====================

    // add a new CS, eval metric
    CPCallRecord* insert(CPCallRecord& rec);
    // completely remove candidate
    bool removeCandidate(CallList::iterator& candidate);
    bool removeIgnored(CallList::iterator& candidate);
    bool remove(CallSite cs);
    // move candidate to ignored list
    bool ignoreCandidate(CallList::iterator& candidate);
    bool ignore(CallSite cs);
    unsigned removeDeadCallee(Function* func);
    CallList::iterator findCandidate(CallSite cs);
    CallList::iterator findIgnored(CallSite cs);
    bool sanityCheckLists();

    CallerMap _callers;    // Function --> calling call sites
    CallList  _candidates; // sorted ascending by mval
    CallMap   _records;    // call site --> call record (in _candidates)
    CallList  _ignore;     // Needed in case they are inlined by another CS

    std::set<CallSite> _removed;

    // =====================
    // Evaluation metrics
    // =====================

    FuncAttrMap* _funcAttr;   // code attribute cache


    // =====================
    // Inlining
    // =====================

    // CS is removed on success, ignored on failure.
    // Any inlined CSs are inserted.

    bool inlineIfPossible(CallSite CS, InlineFunctionInfo &IFI,
                          InlinedArrayAllocasTy &InlinedArrayAllocas);

    bool updateCallers(Function* caller);
    int computeBudget(int size);

    unsigned functionZID(Function* f);

    AllocaMap _allocas;   // per-caller inlined array allocas
    IFIMap    _funcInfo;  // per-caller inline function infos

  
  }; // FDOInliner

} // namespace


#endif
