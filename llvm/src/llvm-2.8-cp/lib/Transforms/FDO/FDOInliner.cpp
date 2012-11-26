//===- FDOInliner.cpp - Feedback-Directed Function Inliner ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
//  Paul Berube, June 13, 2011
//
//===----------------------------------------------------------------------===//
//
// A Feedback-Directed Inliner.
//
//
//  TODO:
//    - tune scaling on budget function
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "FDOInliner"
#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetData.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/FDO.h"
#include "llvm/Analysis/CombinedProfile.h"
#include "llvm/Analysis/CPHistogram.h"
#include "llvm/Analysis/CPFactory.h"

#include "llvm/Transforms/FDO/FDOInlinerPass.h"
#include "llvm/Transforms/FDO/TStream.h"

#include <limits>

using namespace llvm;

//STATISTIC(FDOInlineCount, "Counts number of function inlining opportunities");


// if FDIBudget == 1, automatically compute budget
static cl::opt<unsigned>
FDIBudget("FDI-budget", cl::Hidden, cl::init(1),
        cl::desc("FDO inlining code-growth budget (IR instructions)"));

static cl::opt<std::string> 
CPCallFile("FDI-cprof", cl::init("call.cp"), 
           cl::desc("FDO Inlining combined call-profile file name"));

static cl::opt<std::string> 
FDIMetric("FDI-metric", cl::init("mean"), 
          cl::desc("FDO Inlining metric name"));

static cl::opt<unsigned> 
FDIDepth("FDI-depth", cl::init(0), 
          cl::desc("FDO Inlining maximum call-string depth"));

static cl::opt<std::string> 
FDILogBase("FDI-log", cl::init("FDIlog"), 
          cl::desc("FDO Inlining logging basename"));

static cl::opt<unsigned> 
FDIVerbose("FDI-verbose", cl::init(vl::info), 
          cl::desc("FDO Inlining verbosity level"));


char FDOInliner::ID = 0;
INITIALIZE_PASS(FDOInliner, "FDOInliner", "FDO Inliner Pass", false, false);

ModulePass* llvm::createFDOInlinerPass() { return new FDOInliner(); }

/// For this class, we declare that we require and preserve
/// the call graph.
void FDOInliner::getAnalysisUsage(AnalysisUsage &Info) const {
  Info.addRequired<CallGraph>();
  Info.setPreservesAll();
}



//  *********** FDOInliner ****************  //

FDOInliner::~FDOInliner()
{
  CPCallRecord::freeStaticData();
  if(countFD != NULL)
    countFD->close();
  if(csevalFD != NULL)
    csevalFD->close();
  if(deadFD != NULL)
    deadFD->close();
  if(hashFD != NULL)
    hashFD->close();
  if(debugFD != NULL)
    debugFD->close();
}


llvm::raw_fd_ostream* FDOInliner::initLog(TStream& ts, 
                                          const std::string& suffix, unsigned p)
{
    std::string error;
    std::string filename = FDILogBase + suffix;
    llvm::raw_fd_ostream* fd = new llvm::raw_fd_ostream(filename.c_str(), error);
    if(error != "")
    {
      errs() << error << "\n";
      delete fd;
      return(NULL);
    }

    ts.addStream(fd, p);
    return(fd);
}

FDOInliner::FDOInliner() : ModulePass(ID)
{

  // create the debug stream, overriding stderr priority
  debug = TStream(FDIVerbose, true);

  // add log files, or stdout if basename is '-'
  if(FDILogBase == "-")
  {
    count.addStream(&outs(), vl::log);
    cseval.addStream(&outs(), vl::log);
    dead.addStream(&outs(), vl::log);
    hashlog.addStream(&outs(), vl::log);
  }
  else
  {
    // create TStreams with correct default message priority
    count.setDefaultPriority(vl::log);
    cseval.setDefaultPriority(vl::log);
    dead.setDefaultPriority(vl::log);
    hashlog.setDefaultPriority(vl::log);

    // open files and add them to the tees
    // we need these handles so we can close the files when we're done.
    countFD = initLog(count, ".count");
    csevalFD = initLog(cseval, ".cseval");
    deadFD = initLog(dead, ".dead");
    hashFD = initLog(hashlog, ".hash");

    if(FDIVerbose == vl::never)
    {
      debugFD = NULL;
    }
    else
    {
      debugFD = initLog(debug, ".debug");

      count.addStream(debugFD, FDIVerbose);
      cseval.addStream(debugFD, FDIVerbose);
      dead.addStream(debugFD, FDIVerbose);
      hashlog.addStream(debugFD, FDIVerbose);
    }
  } // FDILogBase != '-'

  debug(vl::trace) << "FDOInliner ctor finished\n";

}

// Returns total program size (IR instructions), or 0 on error.
// Loads a combined call profile, copies histograms to call records,
// and then frees the profile.
unsigned FDOInliner::initialize(Module& M, CallGraph& CG, const TargetData* TD)
{

  debug(vl::trace) << "--> FDOInliner::initialize\n";

 // Load Call Profiling info
  CPFactory* fact = new CPFactory(M);
  fact->buildProfiles(CPCallFile);

  if( !fact->hasCallCP() )
  {
    debug(vl::error) << "FDOInliner: no call profile found in file '" 
                     << CPCallFile << "'\n";
    return(0);
  }

  CombinedCallProfile* callCP = fact->takeCallCP();
  delete fact;

  // set the correct metric
  std::string& metric = FDIMetric;
  if( !CPCallRecord::selectMetric(metric) )
  {
    debug(vl::error) << "FDOInliner: could not select metric " << metric 
                     << "\n";
    return(0);
  }

  // connect call records to function size cache
  _funcAttr = CPCallRecord::getFuncAttrMap();
  if(_funcAttr == NULL)
  {
    debug(vl::error) << "FDOInliner: could not get function attribute map\n";
    return(0);
  }

  debug(vl::trace) << "    Initializing function data structures\n";

  // Initialize function attribute cache, per-function IFIs
  // Accumulate total code size
  InlineFunctionInfo IFI = InlineFunctionInfo(&CG, TD);
  unsigned totalSize = 0;
  unsigned funcCnt = 1;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F, ++funcCnt) 
  {
    if (F->isDeclaration()) 
      continue;

    debug(vl::verbose) << "      allocas for (" << funcCnt << ") " 
                       << F->getName().str() << "\n";

    // set up the IFIs
    _funcInfo.insert(std::make_pair(&(*F), IFI)); // insert copies

    debug(vl::verbose) << "      " << F->size() << " blocks\n";
    totalSize += CPCallRecord::recalcFunctionAttr(&(*F));
  }
  
  // the callee function might not be processed yet, so we can't
  // evaluate candidates until later...

  debug(vl::trace) << "    Scanning for inlining candidates in " 
                   << funcCnt << " functions...\n";
  // Scan for call sites and create call records.
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) 
  {
    for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) 
    {
      debug(vl::verbose) << "        " << BB->getName().str() << ": " 
                         << BB->size() << " instructions\n";
      for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) 
      {
        if(isFDOInliningCandidate(I))
        {
          // CS is a potentially-inlineable call, create the call record
          CallSite cs(cast<Value>(I));
          BasicBlock* bb = BB;
          CPHistogram& cp = (*callCP)[bb];
          Function* caller = cs.getCaller();
          Function* callee = cs.getCalledFunction();
          CPCallRecord rec = CPCallRecord(cs, &cp);
          //rec.evalMetric();
          
          debug(vl::verbose) << "CallSite " << rec.ID << ": ";
          CPCallRecord::printCS(debug, "", cs, "", bb, caller, callee);
          debug << ((cs.getInstruction() == NULL) ? " (NULL)\n" : " (OK)");
          
          // keep track of the callers of each function so we can
          // update their metrics if we inline into their callee
          _callers[callee].insert(cs);
          
          // _candidates has record references for inlining candidates
          _candidates.push_back(rec);
          _records[cs] = &(_candidates.back());
          debug(vl::verbose) << " C\n";
        } // isFDOInliningCandidate
      } // for instruction in block
      debug(vl::verbose) << "        (finished " << BB->getName().str() << ")\n";
    } // for block in function
    debug(vl::verbose) << "        (finished " << F->getName().str() << ")\n";
  } // for function in module

  delete callCP;

  // now that we have all the info, evaluate the candidates
  debug(vl::info) << "    Re-evaluate mvals\n";
  for(CallList::iterator i = _candidates.begin(), E = _candidates.end();
      i != E; ++i)
  {
    i->evalMetric();
  }

  // sort (asending) all inlining candidates by metric value
  debug(vl::info) << "    Sort canidates\n";
  _candidates.sort();

  CPFactory::freeStaticData();

  debug(vl::trace) << "<-- FDOInliner::initialize\n";

  return(totalSize);
}


bool FDOInliner::runOnModule(Module &M) 
{  
 
  // Get the data structures needed for inlining
  CallGraph &CG = getAnalysis<CallGraph>();
  const TargetData *TD = getAnalysisIfAvailable<TargetData>();

  unsigned totalSize = initialize(M, CG, TD);
  if(totalSize == 0)
  {
    debug(vl::error) << "FDOInliner: Error: Failed to initialize\n";
    exit(-1);
    //return(false);
  }

  if(!sanityCheckLists())
  {
    debug(vl::error) << "FDOInliner: initial sanity check failed\n";
    exit(-1);
    //return(false);
  }

  unsigned numCandidates = _candidates.size();

  // calculate our code-growth budget.
  int initialBudget = computeBudget(totalSize);
  int budget = initialBudget;

  unsigned inlineCount = 0;
  unsigned inlineFail = 0;
  unsigned neverInline = 0;
  unsigned candConvert = 0;
  unsigned missingRecord = 0;
  unsigned tooDeep = 0;
  unsigned tooBig = 0;
  unsigned newCand = 0;
  unsigned newIgnore = 0;
  unsigned newNotCand = 0;
  unsigned endSkip = 0;
  unsigned deadCalls = 0;
  bool didTry = true;
  bool error = false;

  debug(vl::trace) << "Starting Inlining.  Initial budget: " << initialBudget 
                   << "\n";

  // Try to inline (best first) until the budget is consumed or there
  // are no candidates remaining
  while( !error && (budget > 0) && (_candidates.size() > 0) )
  {
    // remember, _candidates is sorted ascending, start at the back
    CPCallRecord& crec = _candidates.back();
    CallList::iterator candIter = --_candidates.end();

    Function* caller = crec.cs.getCaller();
    Function* callee = crec.cs.getCalledFunction();
    
    debug(vl::info) << "Candidate (" << format("%.2f", crec.mval) << "): ";
    crec.print(debug(vl::info));
    debug(vl::info) << "\n";

    if(!didTry) endSkip++;
    didTry = false;

    // no more beneficial candidates?
    if(crec.mval <= 0)
    {
      debug(vl::info) << "    no benefit\n";
      break;
    }

    // candidate is too large? (and more than one caller)
    //if((int)(*_funcAttr)[callee].size > budget) 
    int iSize = crec.inlineSize();
    if(iSize > budget)
    {
      tooBig++;
      debug(vl::info) << "    too big (" << iSize << "/" << budget << ")\n";
      ignoreCandidate(candIter);
      continue;
    }

    didTry = true;
    endSkip = 0;

    if(crec.neverInline())
    {
      neverInline++;
      debug(vl::info) << "    never inline\n";
      ignoreCandidate(candIter);
      continue;
    }

    // respect maximum inlining depth
    if( (FDIDepth > 0) && (crec.history.size() >= FDIDepth) )
    {
      tooDeep++;
      debug(vl::info) << "    too deep (" << crec.history.size() << ")\n";
      ignoreCandidate(candIter);
      continue;
    }


    // If successful, inlining invalidates the CallSite, so we need
    // to do the removal bookkeeping *before* this happens. However,
    // we still need the combined profile so that we can create the
    // correct estimated profiles for any inlined calls.  If
    // inlining fails, we still need to ignore this call.  In this
    // case, the CallSite is still valid, but we don't need the
    // profile.  Remove will delete the whole call record, so we
    // need to make a copy to retain the CallSite and the histogram.
    debug(vl::trace) << "    Removing callsite before inlining attempt\n";
    CPCallRecord tmpRec = CPCallRecord(crec);
    BasicBlock* BB = crec.cs->getParent();
    removeCandidate(candIter);
    // ***
    // *** crec is now INVALID ***
    // ***
    
    // try to inline
    debug(vl::trace) << "    Trying to inline: \n";
    InlineFunctionInfo& ifi = _funcInfo[caller];
    if(!inlineIfPossible(tmpRec.cs, ifi, _allocas[caller]))
    {
      inlineFail++;
      debug(vl::info) << "fail\n";
      ignore(tmpRec.cs);  // re-insert because of the initial remove
      // PB Should probably mark the callee as cannotInline...
      //_funcAttr[callee]->cannotInline = true;
      //error = true;
      //break;
      continue;        
    }

    // Inlining successful!
    inlineCount++;
    (*_funcAttr)[caller].inlineCount += (*_funcAttr)[callee].inlineCount + 1;
    
    // print the call record
    debug(vl::log) << "  ";
    tmpRec.print(debug, BB, caller, callee);
    debug << " inlined (" << budget << "), (" 
          << _callers[callee].size() << " callers left)\n";
    
    //int expectedGrowth = (*_funcAttr)[callee].size;
    int codeGrowth = CPCallRecord::recalcFunctionAttr(caller);
    budget -= codeGrowth;
    unsigned callerBlocks = caller->size();
    unsigned calleeBlocks = callee->size();
    debug(vl::verbose) << "    Blocks: caller: " << callerBlocks 
                       << ", callee: " << calleeBlocks << " --> " 
                       << caller->size() << "\n" << "    Expected growth: " 
                       << iSize << ", real growth: " 
                       << codeGrowth << " (" << budget << ")\n";

    // process any callsites that got inlined
    if(!ifi.InlinedCalls.empty())
    {
      unsigned numInlinedCalls = ifi.InlinedCalls.size();
      debug(vl::info) << "    Inlined " << numInlinedCalls << " call sites:\n";
      
      /*
      // we'd better have an origin entry for every inlined call
      if(numInlinedCalls != ifi.InlinedCallOrigins.size())
      {
        errs() << "Mismatch: " << numInlinedCalls << " calls in IFI, but " 
               << ifi.InlinedCallOrigins.size() << " origins\n";
        error = true;
        break;
      }
      */
      
      for(unsigned i = 0; i != numInlinedCalls; ++i)
      {
        CallSite newCS = CallSite(ifi.InlinedCalls[i]);
        CallSite oldCS = CallSite(ifi.InlinedCallOrigins[i]);
        
        CPCallRecord::printCS(debug(vl::info), "      ", newCS, " ");
        
        if(!ifi.InlinedCallOrigins[i]) 
        {
          debug(vl::info) << "(invalid origin)\n";
          error = true;
          break;
        }
        
        // do nothing if it's not a candidate
        if(!isFDOInliningCandidate(newCS.getInstruction()))
        {
          newNotCand++;
          debug(vl::info) << "(not candidate)\n";
          continue;
        }
        
        // it's not intrinsit or icall, so record the new caller
        _callers[newCS.getCalledFunction()].insert(newCS);

        // check for icall->direct call resolution
        // ignore because we don't have a CP for it
        if( (oldCS.getCalledFunction() == NULL) 
            && (newCS.getCalledFunction() != NULL))
        {
          debug(vl::info) << "(newly resolved)\n";
          candConvert++;
          ignore(newCS);
          continue;
        } 
        
        // get the record for the old call site
        CallMap::iterator recIter = _records.find(oldCS);
        
        // we should have a record for the oldCS.  If not, we can't
        // build one for the newCS
        if(recIter == _records.end())
        {
          missingRecord++;
          //ignore(newCS);
          debug(vl::info) << " (missing record!)\n";
          error = true;
          break;
        }
        
        // if we're already ignoring the original call site, ignore
        // the inlined copy also
        if(recIter->second->ignored)
        {
          newIgnore++;
          debug(vl::info) << " (i)\n";
          ignore(newCS);
          continue;
        }

        // Otherwise, we have a valid new inlining candidate
        newCand++;
        CPCallRecord rec = CPCallRecord(tmpRec, *(recIter->second), 
                                        callee, newCS);
        debug(vl::info) << " " << rec.historyString.size() << "  mval=" 
                        << rec.mval << "\n";
        insert(rec);
      } // for inlined calls
    } // if inlined calls
    
    // now that all the inlined calls have been processed, check if
    // the callee is dead (recursively)
    if(_callers[callee].size() == 0)
    {
      unsigned removedCalls = removeDeadCallee(callee);
      debug(vl::info) << "    " << removedCalls << " calls removed\n";
      deadCalls += removedCalls;
    }
    
    // recalculate metrics for the callers of the caller to take
    // into account the inlining we just did
    if(!updateCallers(caller))
    {
      debug(vl::error) << "Failed to update callers of " 
                       << caller->getName().str() << "\n";
      error = true;
      break;
    }

    if(!sanityCheckLists())
    {
      debug(vl::error) << "FDOInliner: sanity check failed\n";
      error = true;
      break;
    }
    
  } // inlining loop

  // If something went wrong, bail now.
  if(error)
  {
    debug(vl::error) << "\n\nFDO Inlining finished with errors\n\n";
    CPFactory::freeStaticData();
    return(inlineCount > 0);
  }


  debug(vl::info) << "\n\nFDO Inlining finished\n\n";

  finalReport(M);

  unsigned zeroCand = 0;
  for(CallList::iterator c = _candidates.begin(), E = _candidates.end(); 
      c !=E; ++E)
    if(c->mval <= 0) zeroCand++;
  
  count() << "  Calls inlined:   " << inlineCount << "\n"
          << "  Failures:        " << inlineFail << "\n"
          << "  Initial cands.:  " << numCandidates << "\n"
          << "  New Candidates:  " << newCand << "\n"
          << "  Never Inline:    " << neverInline << "\n"
          << "  New ignored:     " << newIgnore << " ("<< _ignore.size()<< " toal)\n"
          << "  New non-cand:    " << newNotCand << "\n"
          << "  Resolve/Convert: " << candConvert << "\n"
          << "  Missing records: " << missingRecord << "\n"
          << "  Rejected (deep): " << tooDeep << "\n"
          << "  Rejected (big):  " << tooBig - endSkip << "\n"
          << "  Calls made dead: " << deadCalls 
          << " (" << _removed.size() << " removed)\n"
          << "  Candidates left: " << _candidates.size() + endSkip 
          << " (" << zeroCand << " w/ 0 mval)\n"
          << "  Budget left:     " << budget << " of " << initialBudget 
          << " (+" << format("%0.1f", 
                             100.0*(double)initialBudget/(double)totalSize)
          << " of " << totalSize
          << ")\n";


  CPCallRecord::freeStaticData();

  return(inlineCount > 0);
}


//===================================================================//
//                                                                   //
//     OPERATIONS                                                    //
//                                                                   //
//===================================================================//



// linear-scan sorted (ascending) insertion
CPCallRecord* FDOInliner::insert(CPCallRecord& rec)
{
  debug(vl::detail) << "-->FDOInliner::insert(rec)\n";

  CallList::iterator iter = _candidates.begin();
  CallList::iterator End = _candidates.end();

  while( (iter != End) && ((*iter) < rec) )
    ++iter;

  // iterator --> CPCallRecord --> CPCallRecord*
  CPCallRecord* where = &(*(_candidates.insert(iter, rec)));
  _records[rec.cs] = where;
  
  // putting ignored records in _candidates is semantically wrong
  if(where->ignored)
  {
    where->ignored = false;
    debug(vl::warn) << "FDOInliner::insert Warning: ignored record inserted; set not-ignored: \n";
    where->print(debug(vl::warn));
    debug << "\n";
  }

  debug(vl::detail) << "<-- FDOInliner::insert\n";

  return(where);
}


// move the candidate from candidates to ignore
bool FDOInliner::ignoreCandidate(CallList::iterator& candidate)
{
  debug(vl::detail) << "--> FDOInliner::ignoreCandidate\n";

  // make sure candidate is set ignored
  candidate->ignored = true;

  // move candidate to front of ignore
  _ignore.splice(_ignore.begin(), _candidates, candidate);

  // iterator --> pointer
  CPCallRecord* rec = &(*(_ignore.begin()));

  // make sure mapping is valid
  _records[rec->cs] = rec;

  debug(vl::detail) << "<-- FDOInliner::ignoreCandidate\n";

  return(true);
}


bool FDOInliner::ignore(CallSite cs)
{
  debug(vl::detail) << "--> FDOInliner::ignore(cs)\n";

  CallList::iterator cand = findCandidate(cs);

  if(cand == _candidates.end())
  {
    // either already ignored, or does not exist.  If not ignored,
    // create a new record to ignore
    if(findIgnored(cs) == _ignore.end())
    {
      CPCallRecord newrec = CPCallRecord(cs);
      newrec.ignored = true;
      _ignore.push_front(newrec);
      _records[cs] = &(_ignore.front());
    }
    return(true);
  }

  debug(vl::detail) << "<-- FDOInliner::ignore(cs)\n";

  // ignore the candidate
  return(ignoreCandidate(cand));
}


// delete a call record from the candidates list
bool FDOInliner::removeCandidate(CallList::iterator& candidate)
{
  debug(vl::detail) << "--> FDOInliner::removeCandidate\n";

  if(candidate == _candidates.end())
  {
    debug(vl::error) << "FDOInliner::remove Error: candidate is end of list\n";
    return(false);
  }

  CPCallRecord* rec = &(*candidate);

  if(rec == NULL)
  {
    debug(vl::error) << "FDOInliner::removeCandidate Error: null call record\n";
    return(false);
  }

  CPCallRecord::printCS(debug(vl::verbose), "removing: ", rec->cs, "\n");

  // cs is no longer a caller
  Function* callee = rec->cs.getCalledFunction();
  if(callee != NULL)
    _callers[callee].erase(rec->cs);

  _records.erase(rec->cs);        // remove map entry
  _candidates.erase(candidate);   // free the record

  _removed.insert(rec->cs);

  debug(vl::detail) << "<-- FDOInliner::removeCandidate\n";

  return(true);
}

// delete a call record from the ignored list
bool FDOInliner::removeIgnored(CallList::iterator& ignored)
{
  debug(vl::detail) << "--> FDOInliner::removeIgnored\n";

  if(ignored == _ignore.end())
  {
    debug(vl::error) << "FDOInliner::removeIgnored Error: ignored is end of list\n";
    return(false);
  }

  CPCallRecord* rec = &(*ignored);

  if(rec == NULL)
  {
    debug(vl::error) << "FDOInliner::removeIgnored Error: null ignored call record\n";
    return(false);
  }

  CPCallRecord::printCS(debug(vl::verbose), "removing: ", rec->cs, "\n");

  // cs is no longer a caller
  Function* callee = rec->cs.getCalledFunction();
  if(callee != NULL)
    _callers[callee].erase(rec->cs);

  _records.erase(rec->cs);  // remove map entry
  _ignore.erase(ignored);   // free the record

  _removed.insert(rec->cs);

  debug(vl::detail) << "<-- FDOInliner::removeIgnored\n";

  return(true);
}


bool FDOInliner::remove(CallSite cs)
{
  debug(vl::detail) << "--> FDOInliner::remove(cs)\n";

  if(_removed.find(cs) != _removed.end())
  {
    CPCallRecord::printCS(debug(vl::error), 
                          "FDOInliner::remove Already removed callsite: ",
                          cs, "\n");
    return(false);
  }


  CallMap::iterator recIter = _records.find(cs);
  if(recIter == _records.end())
  {
    CPCallRecord::printCS(debug(vl::error), 
                          "FDOInliner::remove Error: no record of callsite: ",
                          cs, "\n");
    return(false);
  }

  if(recIter->second->ignored)
  {
    debug(vl::info) << " (i)";
    // try to remove as ignored
    CallList::iterator ignored = findIgnored(cs);
    if(ignored != _ignore.end())
      return(removeIgnored(ignored));
    debug(vl::info) << " ignored not found\n";
  }
  else
  {
    debug(vl::info) << " ( )";
    // not ignored; try to remove as a candidate
    CallList::iterator cand = findCandidate(cs);
    if(cand != _candidates.end())
      return(removeCandidate(cand));
    debug(vl::info) << " candidate not found\n";
  }

  debug(vl::error) << "\nError: failed to remove:\n";
  recIter->second->print(errs());
  errs() << "\n";
  return(false);
}


// returns _candidates.end() if call site is not found.
CallList::iterator FDOInliner::findCandidate(CallSite cs)
{
  debug(vl::detail) << "<-- FDOInliner::findCandidate(cs)\n";

  CallMap::iterator mapentry = _records.find(cs);

  if( mapentry == _records.end() )
    return(_candidates.end());

  CPCallRecord* rec = mapentry->second;
  
  // not in candidates if it's ignored.
  if(rec->ignored)
    return(_candidates.end());

  // need to search candidates to get correct iterator
  CallList::iterator c = _candidates.begin();
  for(CallList::iterator E = _candidates.end(); c != E; ++c)
    if( &(*c) == rec ) break;

  debug(vl::detail) << "<-- FDOInliner::findCandidate\n";

  return(c);
}

// returns _ignore.end() if call site is not found.
CallList::iterator FDOInliner::findIgnored(CallSite cs)
{
  debug(vl::detail) << "--> FDOInliner::findIgnored(cs)\n";

  CallMap::iterator mapentry = _records.find(cs);

  if( mapentry == _records.end() )
    return(_ignore.end());

  CPCallRecord* rec = mapentry->second;
  
  // if not ignored, we won't find it in the ignore list!
  if(!rec->ignored)
    return(_ignore.end());

  // need to search ignore to get correct iterator
  CallList::iterator c = _ignore.begin();
  for(CallList::iterator E = _ignore.end(); c != E; ++c)
    if( &(*c) == rec ) break;

  debug(vl::detail) << "<-- FDOInliner::findIgnored\n";

  return(c);
}


// returns number of dead calls removed
unsigned FDOInliner::removeDeadCallee(Function* func)
{
  debug(vl::trace) << "--> FDOInliner::removeDeadCallee\n";

  unsigned removedCalls = 0;
  std::set<Function*> callees;

  if(func == NULL) return(0);

  // check if llvm thinks this callee is dead ??
  func->removeDeadConstantUsers();
  // from Function::isDefTriviallyDead() in later versions
  bool llvmLinkDead = true;
  bool llvmUseDead = true;
  // Check the linkage
  if( !func->hasLinkOnceLinkage() 
      && !func->hasLocalLinkage() 
      && !func->hasAvailableExternallyLinkage() )
    llvmLinkDead = false;
  // Check if the function is used by anything other than a blockaddress.
  if(llvmLinkDead)
    for(Value::use_iterator I = func->use_begin(), E = func->use_end(); 
        I != E; ++I)
      if (!isa<BlockAddress>(*I))
      {
        llvmUseDead = false;
        break;
      }
  bool llvmDead = llvmLinkDead && llvmUseDead;

  // check if we think the callee is dead
  bool fdiDead = ( (_callers[func].size() == 0) 
                   && !(*_funcAttr)[func].addressTaken );

  if(llvmDead != fdiDead)
  {
    debug(vl::warn) << "Warning: Dead-callee disagreement (" 
                    << func->getName().str() << "): llvm: " << llvmLinkDead 
                    << "," << llvmUseDead << ", fdi: " << fdiDead << "\n";
    //return(0);
  }

  // he's not dead, jim
  if(!fdiDead) return(0);

  debug(vl::info) << "Callee is dead: " << func->getName().str() << "\n";

  // find and remove calls for recursive dead callee removal
  for(Function::iterator BB = func->begin(), EB = func->end(); BB != EB; ++BB)
    for(BasicBlock::iterator I = BB->begin(), EI = BB->end(); I != EI; ++I)
      if(isFDOInliningCandidate(&(*I)))
      {
        CallSite cs(cast<Value>(I));
        Function* callee = cs.getCalledFunction();
        if(callee != NULL)
          callees.insert(callee);

        CPCallRecord::printCS(debug(vl::info), "      Removing: ",
                              cs, "");
        if(remove(cs))
        {
          removedCalls++;
          debug(vl::info) << "\n";
        }
        else
          debug(vl::info) << " FAILED\n";
          
      }


  // recursively remove callees
  for(std::set<Function*>::iterator callee = callees.begin(), E = callees.end();
      callee != E; ++callee)
    removedCalls += removeDeadCallee(*callee);

  debug(vl::trace) << "<-- FDOInliner::removeDeadCallee\n";

  return(removedCalls);
}


// a Function's zID is the sum of the zID's of all the inlining
// candidates in the function.
unsigned FDOInliner::functionZID(Function* F)
{
  debug(vl::trace) << "--> FDOInliner::functionZID\n";

  unsigned zID = 0;
  for(Function::iterator BB = F->begin(), BBE = F->end(); BB != BBE; ++BB)
    for(BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ++I)
      if(isFDOInliningCandidate(I))
      {
        CallSite cs(cast<Value>(I));
        //Function* callee = cs.getCalledFunction();
        CallMap::iterator recIter = _records.find(cs);
        if(recIter == _records.end())
        {
          //errs() << "FDOInliner::getFunctionZID Error: "
          //       << "no record for candidate " << F->getName().str() 
          //       << "[" << BB->getName().str() << "] --> " 
          //       << callee->getName().str() << "\n";
          
          continue;
        }
        zID += recIter->second->zID;
      }

  debug(vl::trace) << "<-- FDOInliner::functionZID\n";

  return(zID);
}
      



bool FDOInliner::sanityCheckLists()
{
  debug(vl::detail) << "--> FDOInliner::sanityCheckLists\n";

  bool sane = true;

  for(CallList::iterator c = _candidates.begin(), E = _candidates.end(); 
      c != E; ++c)
    if(c->ignored)
    {
      debug(vl::error) << "Error: ignored candidate: ";
      c->print(errs());
      errs() << "\n";
      sane = false;
    }

  for(CallList::iterator c = _ignore.begin(), E = _ignore.end(); c != E; ++c)
    if(!(c->ignored))
    {
      debug(vl::error) << "Error: not-ignored ignore: ";
      c->print(errs());
      errs() << "\n";
      sane = false;
    }
    
  debug(vl::detail) << "<-- FDOInliner::sanityCheckLists\n";

  return(sane);
}

//===================================================================//
//                                                                   //
//      INLINING                                                     //
//                                                                   //
//===================================================================//


// use FDIBudget, or compute if FDIBudget==1.  Unlimitted budget if
// FDIBudget==0
int FDOInliner::computeBudget(int size)
{
  debug(vl::detail) << "--> FDOInliner::computeBudget\n";

  int b = FDIBudget;

  if(FDIBudget == 0)
  {
    b = std::numeric_limits<int>::max();
  }
  else if(FDIBudget == 1)
  {
    const double minPct = 0.05;      // y-shift on sqrt(size)
    const double maxPct = 10.0;      // upper-bound
    double growthFactor;

    // Sizes:
    //   gzip (real):    6748
    //   bzip (real):   11251
    //   gobmk (spec):  91778
    //   gcc (spec):   407976
    
    // Formula is only defined between these sizes, and is calibrated
    // to hit (maxPct+minPct) at minSize, and minPct at maxSize
    const double maxSize = 425000;
    const double minSize = 5000;
    const double scale = maxPct / (1/sqrt(minSize) - 1/sqrt(maxSize));

    if(size >= maxSize)
      growthFactor = minPct;
    else if(size <= minSize)
      growthFactor = maxPct;
    else
      growthFactor = scale * ( 1/sqrt(size) - 1/sqrt(maxSize) ) + minPct;

    // These checks should be unnecessary now...
    if(growthFactor < minPct) growthFactor = minPct;
    if(growthFactor > maxPct) growthFactor = maxPct;
    b = (int)floor(growthFactor*size);
  }

  debug(vl::info) << "** Inlining Budget: " << size
                  << " +" << format("%2.1f", 100.0*b/size) << "% = " 
                  << b << "\n";

  debug(vl::detail) << "<-- FDOInliner::computeBudget\n";
  return(b);
}


// COPIED FROM: Transforms/IPO/Inliner.cpp
//  (original name: InlineCallIfPossible)
//  (see that file for full comments)
//
/// InlineCallIfPossible - If it is possible to inline the specified call site,
/// do so and update the CallGraph for this operation.
///
// Track allocas, merge them if possible
bool FDOInliner::inlineIfPossible(CallSite CS, InlineFunctionInfo &IFI,
                                 InlinedArrayAllocasTy &InlinedArrayAllocas) 
{
  Function *Callee = CS.getCalledFunction();
  Function *Caller = CS.getCaller();

  // Try to inline the function.  Get the list of static allocas that were
  // inlined.
  if (!InlineFunction(CS, IFI))
    return false;

  // If the inlined function had a higher stack protection level than the
  // calling function, then bump up the caller's stack protection level.
  if (Callee->hasFnAttr(Attribute::StackProtectReq))
    Caller->addFnAttr(Attribute::StackProtectReq);
  else if (Callee->hasFnAttr(Attribute::StackProtect) &&
           !Caller->hasFnAttr(Attribute::StackProtectReq))
    Caller->addFnAttr(Attribute::StackProtect);

  
  // Look at all of the allocas that we inlined through this call site.  If we
  // have already inlined other allocas through other calls into this function,
  // then we know that they have disjoint lifetimes and that we can merge them.
  // [snip]
  SmallPtrSet<AllocaInst*, 16> UsedAllocas;
  
  // Loop over all the allocas we have so far and see if they can be merged with
  // a previously inlined alloca.  If not, remember that we had it.
  for (unsigned AllocaNo = 0, e = IFI.StaticAllocas.size();
       AllocaNo != e; ++AllocaNo) {
    AllocaInst *AI = IFI.StaticAllocas[AllocaNo];
    
    // Don't bother trying to merge array allocations, or allocations whose
    // type is not itself an array (because we're afraid of pessimizing SRoA).
    const ArrayType *ATy = dyn_cast<ArrayType>(AI->getAllocatedType());
    if (ATy == 0 || AI->isArrayAllocation())
      continue;
    
    // Get the list of all available allocas for this array type.
    std::vector<AllocaInst*> &AllocasForType = InlinedArrayAllocas[ATy];
    
    // Loop over the allocas in AllocasForType to see if we can reuse one.
    bool MergedAwayAlloca = false;
    for (unsigned i = 0, e = AllocasForType.size(); i != e; ++i) {
      AllocaInst *AvailableAlloca = AllocasForType[i];
      
      // The available alloca has to be in the right function, not in some other
      // function in this SCC.
      if (AvailableAlloca->getParent() != AI->getParent())
        continue;
      
      // If the inlined function already uses this alloca then we can't reuse
      // it.
      if (!UsedAllocas.insert(AvailableAlloca))
        continue;
      
      // Otherwise, we *can* reuse it, RAUW AI into AvailableAlloca and declare
      // success!
      //DEBUG(dbgs() << "    ***MERGED ALLOCA: " << *AI);
      
      AI->replaceAllUsesWith(AvailableAlloca);
      AI->eraseFromParent();
      MergedAwayAlloca = true;
      //++NumMergedAllocas;
      break;
    }

    // If we already nuked the alloca, we're done with it.
    if (MergedAwayAlloca)
      continue;

    // If we were unable to merge away the alloca either because there are no
    // allocas of the right type available or because we reused them all
    // already, remember that this alloca came from an inlined function and mark
    // it used so we don't reuse it for other allocas from this inline
    // operation.
    AllocasForType.push_back(AI);
    UsedAllocas.insert(AI);
  }
  
  return true;
}



// update mval for callers of the caller (needed if they use _funcsize)
bool FDOInliner::updateCallers(Function* caller)
{
  debug(vl::detail) << "--> FDOInliner::updateCallers\n";

  if(caller == NULL)
  {
    debug(vl::error) << "FDOInliner::updateCallers Error: NULL caller\n";
    return(false);
  }


  CallerSet callers = _callers[caller];
  debug(vl::info) << "  Updating " << callers.size() << " callers: ";
  for(CallerSet::iterator c = callers.begin(), E = callers.end(); 
      c != E; ++c)
  {
    unsigned cnt = _records.count(*c);
    if(cnt == 1) 
    {
      //errs() << ".";
    }
    else errs() << cnt;

    if(cnt == 0)
    {
      debug(vl::error) << "\nFDOInliner::updateCallers " 
                       << "Error: no record for caller: "
                       << caller->getName().str() << "\n";
      return(false);
    }
    else
    {          
      CPCallRecord* callerRec = _records[*c];
      if(callerRec == NULL)
      {
        debug(vl::error) << "\nFDOInliner::updateCallers Error: NULL record\n";
        return(false);
      }
      else
      {
        if(!callerRec->ignored)
          callerRec->evalMetric();
      }
    }
  }
  
  debug(vl::info) << " (done)\n";

  return(true);
}


//===================================================================//
//                                                                   //
//      CALLSITE EXCLUSION                                           //
//                                                                   //
//===================================================================//


// Basic checking to see if an instruction is an inlining candidate
bool FDOInliner::isFDOInliningCandidate(Instruction* I)
{
  if(I == NULL) return(false);
  
  CallSite cs(cast<Value>(I));
  // Not a call instruction
  if(!cs) return(false);
  
  // Intrinsics can never be inlined
  if(isa<IntrinsicInst>(I)) return(false);
  
  Function* callee = cs.getCalledFunction();
  
  // Indirect calls cannot be inlined (ignore the possibility they
  // might resolve to direct calls later)
  if(callee == NULL) return(false);
  
  // Ignore immediately-recursive calls.
  if(callee == cs.getCaller()) return(false);
  
  // Can't inline without the definition (assumes whole-program analysis)
  if(callee->isDeclaration()) return(false);
  
  // We're out of excuses
  return(true);
}


// check if there is at least one inlining candidate in this BB
bool FDOInliner::hasFDOInliningCandidate(BasicBlock* BB)
{
  if(BB == NULL) return(false);
  
  for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) 
  {
    // One call is enough
    if(isFDOInliningCandidate(&(*I)))
      return(true);
  }
  
  return(false);
}


//===================================================================//
//                                                                   //
//      REPORTS                                                      //
//                                                                   //
//===================================================================//


// Output the hash log:
// <S> <zID> <FName> <inSize> <outSize> [inline history...]
// where S is the status:
//  - N  new              (zID <- 0, since no inlining)
//  - D  dead             (zID <- 0, because it's now irrelevant)
//  - 0  not inlined-into (zID == 0)
//  - I  inlined-into     (only these last two have anything past FName)
//  - X  inlined-into but cannot be inlined
void FDOInliner::finalReport(Module& M)
{
  // Global Hash = XOR of all non-dead funcs zIDs
  //  (func zID = SUM of its call records' zIDs)
  //  (call record zID = random init, XOR of recs on inlining chain)
  unsigned globalHash = 0;

  debug(vl::detail) << "--> FDOInliner::finalReport\n";

  for(Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
  {
    if(F->isDeclaration()) continue;


    // find the attribute record, or discover a new function (should
    // never happen)
    FuncAttrMap::iterator attrIter = _funcAttr->find(&(*F));
    if(attrIter == _funcAttr->end())
    {
      debug(vl::warn) << F->getName().str() << " NEW!!\n";
      hashlog() << "N 00000000 " << F->getName().str() << "\n";
      continue;
    }

    // get the zID.  Also scans for missing records
    // wait until we know the func isn't dead to update the global hash
    unsigned zID = functionZID(&(*F));
    FunctionAttr* attr = &(attrIter->second);

    // is this function dead code now?
    if( (_callers[&(*F)].size() == 0) && !attr->addressTaken 
        && (F->getName() != "main") )
    {
      dead() << F->getName().str() << " " << format("%08X", zID) << "\n";
      hashlog() << "D 00000000 " << F->getName().str() << "\n";
      continue;
    }

    // did anything get inlined into this function?
    if(attr->inlineCount == 0)
    {
      hashlog() << "0 00000000 " << F->getName().str() << "\n";
      continue;
    }

    int start = attr->startSize;
    int end = attr->size;
    
    if(attr->cannotInline)
      hashlog() << "X ";
    else
      hashlog() << "I ";

    hashlog() << format("%08X", zID) << " " << F->getName().str();

    // update the global hash
    globalHash = globalHash ^ zID;

    //hashlog() << " " << start << " " << end << " " << attr->inlineCount << ": ";
    hashlog() << " " << start << " " << end << " " << attr->inlineCount << "\n";
    //               << " @" << _callers[&(*F)].size() 
    //        << (attr->addressTaken ? "&\n" : "\n");

    // find callsites, list their inline history
    for(Function::iterator BB = F->begin(), BE = F->end(); BB != BE; ++BB)
      for(BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ++I)
        if(isFDOInliningCandidate(I))
        {
          CallSite cs(cast<Value>(I));
          Function* callee = cs.getCalledFunction();
          CallMap::iterator recIter = _records.find(cs);
          
          if(recIter == _records.end())
          {
            debug(vl::error) << "  Error: no record for call: " 
                             << F->getName().str() 
                             << "[" << BB->getName().str() << "] --> " 
                             << callee->getName().str() << "\n";
            continue;
          }
          
          CPCallRecord& rec = *(recIter->second);
          
          if(rec.history.size() > 0)
          {
            hashlog() << " [" << BB->getName().str() << "] " 
                      << callee->getName().str() << "{" 
                      << format("%08X", rec.zID) << "}  ";
            rec.printHistory(hashlog(), ",");
            hashlog() << "\n";
          }
        }

  } // for functions

  hashlog() << "Global Hash: " << format("%08X", globalHash) << "\n";
  debug(vl::info) << "Global Hash: " << format("%08X", globalHash) << "\n";

  debug(vl::detail) << "<-- FDOInliner::finalReport\n";

}


