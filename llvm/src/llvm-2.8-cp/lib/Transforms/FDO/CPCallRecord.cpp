//===- CPCallRecord.cpp - (for: Feedback-Directed Function Inliner) -------===//
//
//                     The LLVM Compiler Infrastructure
//
//
//===----------------------------------------------------------------------===//
//
// TODO:
//   - calculate benefit of constant arguments
//   - real CP metrics
//   - CPHistogram.applyRange( (double)(double,double), min, max)
//   - CPHistogram.applyQuantile( (double)(double,double), min, max)
//
//===----------------------------------------------------------------------===//

#include "llvm/IntrinsicInst.h"
#include "llvm/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/Analysis/CPHistogram.h"
#include "llvm/Transforms/FDO/CPCallRecord.h"
#include "llvm/Transforms/FDO/TStream.h"

#include <cstdlib>

using namespace llvm;

// cmd-line opt to specify quantile points for point values and/or
// range endpoints.

static cl::list<double> 
FDIQList("FDI-Q", cl::CommaSeparated, cl::desc("FDI quantile point(s)"));


unsigned CPCallRecord::CurrID = 0;
MetricNameMap CPCallRecord::_metricmap;
FDOInlineMetric CPCallRecord::_metric;

FuncAttrMap CPCallRecord::_funcAttr;  // function attribute cache

// initializing ctor
CPCallRecord::CPCallRecord(CallSite C, const CPHistogram* P, 
                                       double V) : 
  cs(C), mval(V), ignored(false)
{
  ID = CurrID++;
  zID = rand();
  if(P != NULL)
    cphist = new CPHistogram(*P);
  else
    cphist = new CPHistogram();
}

// copy ctor
CPCallRecord::CPCallRecord(const CPCallRecord& rhs) :
  cs(rhs.cs), mval(rhs.mval), ignored(rhs.ignored), history(rhs.history), 
  historyString(rhs.historyString), ID(rhs.ID), zID(rhs.zID)
{
  cphist = new CPHistogram(*(rhs.cphist));
}


// "inlined-call constructor"
// we need to be given the inlinedFunction because inlinedRec.cs
// points to a delted instruction
CPCallRecord::CPCallRecord(const CPCallRecord& callRec, // for inlined call
                           const CPCallRecord& oldRec,  // for original callsite
                           Function* inlinedFunc, // original caller
                           const CallSite newCall) :    // new callsite
  cs(newCall), ignored(false)
{
  ID = CurrID++;
  if( (callRec.cphist != NULL) && (oldRec.cphist != NULL) )
    cphist = callRec.cphist->cross(*(oldRec.cphist));
  else
  {
    errs() << "CPCallRecord::CPCallRecord (inlined) Error: NULL histogram\n";
    cphist = new CPHistogram();
  }

  cs = newCall;
  zID = callRec.zID ^ oldRec.zID;  // summarize history as xor of all 

  // combine the old histories
  history = callRec.history;
  historyString = callRec.historyString;
  history.insert(oldRec.history.begin(), oldRec.history.end());
  historyString.insert(historyString.end(), oldRec.historyString.begin(), 
                       oldRec.historyString.end());

  // incorporate the inlining into the history
  if(inlinedFunc == NULL)
  {
    errs() << "CPCallRecord inlined-call ctor Error: NULL function\n";
    errs() << "  adding (null) to history\n";
    history.insert(0);
    historyString.push_back("(null)");
  }
  else
  {
    //errs() << "  adding " << inlinedFunc->getName().str() << " to history\n";
    history.insert(inlinedFunc);
    historyString.push_back(inlinedFunc->getName());
  }

  evalMetric();
}


CPCallRecord::~CPCallRecord()
{
  if(cphist != NULL)
    delete cphist;
}


void CPCallRecord::freeStaticData()
{
  for(FuncAttrMap::iterator i = _funcAttr.begin(), E = _funcAttr.end();
      i != E; ++i)
  {
    if( (i->second).argImpact != NULL )
      delete[] (i->second).argImpact;
  }
  _funcAttr.clear();
}


bool CPCallRecord::neverInline()
{
  Function* callee = cs.getCalledFunction();

  // can't lookup info for null callee... which incidentally means
  // this is an indirect call that we can't inline anyway.
  if(callee == NULL)
    return(true);

  FunctionAttr* attr = getFunctionAttr(cs.getCalledFunction());

  // did we already determine that the callee should never be inlined?
  if(attr->cannotInline) 
    return(true);

  // Check other conditions.  Update the attr if we find a problem.

  // **** Can we even calculate the alloca size?  especially since
  // we're in SSA right now, so lots of coallescing can be
  // done... ****
  // don't inline funcs with big allocas
  /*
  if(attr->allocaSize > inlineWeights::allocaTooBig)
  {
    attr->cannotInline = true;
    return(true);
  }
  */

  if(callee->doesNotReturn())
  {
    attr->cannotInline = true;
    return(true);
  }

  // nothing stopping inlining
  return(false);
}



void CPCallRecord::printCS(llvm::raw_ostream& stream, const std::string& pre, 
                           CallSite cs, const std::string& post,  
                           BasicBlock* BB, Function* caller, Function* callee)
{
  // pop the raw_ostream into a tee, set to print everything
  // errors and warnings will still also go to stderr, as per TStream default
  TStream tee = TStream(&stream, vl::verbose);
  // delegate to the TStream version
  printCS(tee, pre, cs, post, BB, caller, callee);
}


void CPCallRecord::printCS(TStream& stream, const std::string& pre, 
                           CallSite cs, const std::string& post,  
                           BasicBlock* BB, Function* caller, Function* callee)
{
  stream << pre;

  if(caller == NULL) caller = cs.getCaller();
  if(callee == NULL) callee = cs.getCalledFunction();
  if(BB == NULL) BB = cs->getParent();

  stream << caller->getName().str() << "[" << BB->getName().str() << "]"
         << "(" << _funcAttr[caller].size << ") --";
  if(callee == NULL)
    stream << "*";
  else
    stream << "> " << callee->getName().str() << "(" 
           << _funcAttr[callee].size << ")";

  stream << post;
}


void CPCallRecord::printHistory(llvm::raw_ostream& stream, 
                                const std::string& sep) const
{
  TStream tee = TStream(&stream, vl::verbose);
  printHistory(tee, sep);
}


void CPCallRecord::printHistory(TStream& stream, 
                                const std::string& sep) const
{
  stream << historyString.size() << "[";
  for(unsigned i = 0, E = historyString.size(); i != E; ++i)
  {
    if(i!=0) stream << sep;
    stream << historyString[i];
  }
  stream << "]";  
}


void CPCallRecord::print(llvm::raw_ostream& stream, BasicBlock* BB, 
                         Function* caller, Function* callee) const
{
  TStream tee = TStream(&stream, vl::verbose);
  print(tee, BB, caller, callee);
}

void CPCallRecord::print(TStream& stream, BasicBlock* BB, 
                         Function* caller, Function* callee) const
{
  if(caller == NULL) caller = cs.getCaller();
  if(callee == NULL) callee = cs.getCalledFunction();

  if(BB == NULL) BB = cs->getParent();

  stream << ID << " {" << format("%X",zID) << "}: [" << format("%.4f", mval) 
         << " " << format("%02.0f", 100*cphist->coverage()) << "%] ";
  if(ignored) 
    stream << "(i)";

  printCS(stream, " ", cs, " ", BB, caller, callee);

  printHistory(stream);
}


// returns change in function size vs current size value
int CPCallRecord::recalcFunctionAttr(Function* f)
{
  FunctionAttr* attr;
  bool isNew = false;

  if( (f == NULL) || f->isDeclaration() ) return(0);

  // get or create the attributes record
  FuncAttrMap::iterator attrIter = _funcAttr.find(f);
  if(attrIter == _funcAttr.end())
  {
    attr = &(_funcAttr[f]);
    (*attr) = ZeroFunctionAttr;
    isNew = true;
  }
  else
  {
    attr = &(attrIter->second);
    // don't re-analyze if we already know it can never be inlined...
    //if(attr->cannotInline) return(0);
  }

  // remove trivially-dead references
  f->removeDeadConstantUsers();
  attr->addressTaken = f->hasAddressTaken();

  attr->args = f->arg_size();
  if( (attr->argImpact == NULL) && (attr->args > 0) )
    attr->argImpact = new ArgImpact[attr->args];
  // delay calculating the benefit of a constant argument (to
  // getConstImpact) until we need to eval a call that actually
  // has a constant argument

  // if we did something that invalidated the FunctionAttr (ie,
  // inlined into the function), this probably also invalidates our
  // calculated constImpact: set them all to 0
  for(unsigned i = 0; i < attr->args; i++)
    attr->argImpact[i] = ZeroArgImpact;

  // use the newAttr to calculate the new size
  FunctionAttr newAttr = ZeroFunctionAttr;

  for (Function::iterator BB = f->begin(), E = f->end(); BB != E; ++BB)
  {
    //errs() << "      [" << BB->getName().str() << "] " << bbsize 
    //       << " (" << instrCnt << ")\n";

    // cannot inline indirect branches
    if (isa<IndirectBrInst>(BB->getTerminator()) )
    {
      newAttr.cannotInline = true;
      // it would be good to break here, but we still want to be able
      // to return the real size of the function...
      //break;
    }

    // calcBlockSize will also update call counts and cannotInline
    calcBlockSize(BB, &newAttr);
  }// for blocks

  int growth = newAttr.size - attr->size;
  //errs() << "Out size: " << instrCnt << ", in size: " << attr->size 
  //       << " = " << growth << "\n";

  attr->size = newAttr.size;
  if(isNew)
    attr->startSize = attr->size;

  // selectively copy over recalculated values
  attr->externCalls = newAttr.externCalls;
  attr->directCalls = newAttr.directCalls;
  attr->indirectCalls = newAttr.indirectCalls;
  attr->cannotInline = newAttr.cannotInline;

  return(growth);
}


FunctionAttr* CPCallRecord::getFunctionAttr(Function* F, bool create)
{
  FuncAttrMap::iterator attrIter = _funcAttr.find(F);
  FunctionAttr* attr = NULL;
  if(attrIter == _funcAttr.end())
  {
    if(create)
    {
      recalcFunctionAttr(F);
      attr = &_funcAttr[F];
    }
  }
  else
    attr = &(attrIter->second);

  return(attr);
}


ArgImpact* CPCallRecord::getArgImpact(Function* F, unsigned argNum)
{
  FunctionAttr* attr = getFunctionAttr(F);

  ArgImpact* impact = &(attr->argImpact[argNum]);

  if(argNum >= attr->args)
  {
    errs() << "CPCallRecord::getConstImpact (" << F->getName().str() 
           << ") Error: request for arg " << argNum << " of only " 
           << attr->args-1 << "\n";
    return(impact);
  }
  
  // check if the constImpact has been calculated already
  if( (impact->instrRemIfConst != 0) 
      || (impact->branchRemIfConst != 0) 
      || (impact->icallRemIfConst != 0) 
      || (impact->instrRemIfAlloca != 0) )
    return(impact);

  Function::arg_iterator I = F->arg_begin();
  for(unsigned arg = 0; arg != argNum; ++arg, ++I) {} // seek to argNum
  calcConstantImpact(I, impact);
  calcAllocaImpact(I, impact);

  return(impact);
}


// Originally:  InlineCost:86, CountCodeReductionForAlloca
void CPCallRecord::calcAllocaImpact(Value* V, ArgImpact* rc)
{
  if (!V->getType()->isPointerTy()) return;  // Not a pointer

  for (Value::use_iterator UI = V->use_begin(), E = V->use_end(); UI != E;++UI)
  {
    Instruction *I = cast<Instruction>(*UI);
    if (isa<LoadInst>(I) || isa<StoreInst>(I))
      rc->instrRemIfAlloca++;
    else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I)) 
    {
      // If the GEP has variable indices, we won't be able to do much with it.
      if (GEP->hasAllConstantIndices())
        calcAllocaImpact(GEP, rc);
    } 
    else if (BitCastInst *BCI = dyn_cast<BitCastInst>(I)) 
    {
      // Track pointer through bitcasts.
      calcAllocaImpact(BCI, rc);
    }
    // If there is some other strange instruction, we're not going to be able
    // to do much if we inline this.
  }
}


// Originally CountCodeReductionForConstant from Analysis/InlineCost.cpp
void CPCallRecord::calcConstantImpact(Value *V, ArgImpact* rc) 
{
  if(rc == NULL) return;

  for(Value::use_iterator UI = V->use_begin(), E = V->use_end(); UI != E;++UI)
  {
    User *U = *UI;
    if (isa<BranchInst>(U) || isa<SwitchInst>(U)) 
    {
      rc->branchRemIfConst++;
      // We will be able to eliminate all but one of the successors.
      const TerminatorInst &TI = cast<TerminatorInst>(*U);
      const unsigned NumSucc = TI.getNumSuccessors();
      unsigned totalInstrs = 0;
      for (unsigned i = 0; i != NumSucc; ++i)
        totalInstrs += calcBlockSize(TI.getSuccessor(i));
      // All but one block will be eliminated, but which one?
      // Remove everything except one average-size block
      rc->instrRemIfConst += totalInstrs*(NumSucc-1)/NumSucc;
    } 
    else if (CallInst *CI = dyn_cast<CallInst>(U)) 
    {
      // Turning an indirect call into a direct call is a BIG win
      if (CI->getCalledValue() == V)
        rc->icallRemIfConst++;
    } 
    else if (InvokeInst *II = dyn_cast<InvokeInst>(U)) 
    {
      // Turning an indirect call into a direct call is a BIG win
      if (II->getCalledValue() == V)
        rc->icallRemIfConst++;
    } 
    else 
    {
      // Figure out if this instruction will be removed due to simple constant
      // propagation.
      Instruction &Inst = cast<Instruction>(*U);

      // We can't constant propagate instructions which have effects or
      // read memory.
      if (Inst.mayReadFromMemory() || Inst.mayHaveSideEffects() ||
          isa<AllocaInst>(Inst))
        continue;

      bool AllOperandsConstant = true;
      for (unsigned i = 0, e = Inst.getNumOperands(); i != e; ++i)
        if (!isa<Constant>(Inst.getOperand(i)) && Inst.getOperand(i) != V) 
        {
          AllOperandsConstant = false;
          break;
        }

      if (AllOperandsConstant) 
      {        
        // We will get to remove this instruction...
        rc->instrRemIfConst++;

        // And any other instructions that use it which become constants
        // themselves.
        calcConstantImpact(&Inst, rc);
      }
    }
  }
}


// If attr is supplied, updates call counts, cannotInline.
// Call counts may (will) be inaccurate if cannotInline gets set
unsigned CPCallRecord::calcBlockSize(BasicBlock* BB, FunctionAttr* attr)
{
  // partially incorporates InlineCost:144 CodeMetrics::analyseBasicBlock
  // Did not include:
  // - detect dynamic allocation
  // - count vector instructions
  // - ignore intrinsic instructions
  // - count returns
  
  if(BB == NULL) return(0);

  unsigned size = 0;

  for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) 
  {
    // filter out "free" instructions first:
    
    // Phi-nodes don't count
    if (isa<PHINode>(I))
      continue;
    
    // Debug intrinsics don't count as size.
    if (isa<CallInst>(I) || isa<InvokeInst>(I))
      if (isa<DbgInfoIntrinsic>(I))
        continue;
    
    if (const CastInst *CI = dyn_cast<CastInst>(I)) 
    {
      // Noop casts, including ptr <-> int,  don't count.
      if (CI->isLosslessCast() || isa<IntToPtrInst>(CI) || 
          isa<PtrToIntInst>(CI))
        continue;
      // Result of a cmp instruction is often extended. These are
      // usually nop on most sane targets.
      if (isa<CmpInst>(CI->getOperand(0)))
        continue;
    }
      
    // If a GEP has all constant indices, it will probably be folded
    // with a load/store.
    if (const GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(I) )
    {
      if (GEPI->hasAllConstantIndices())
        continue;
    }

    // everything else counts as an instruction.
    size++;
    // only bother inspecting instructions if we have a FunctionAttr to update
    if(attr == NULL) continue;
    // ... and we might actually want the information
    if(attr->cannotInline) continue;

    // Count local arrays allocated on the stack
    //if(const AllocaInst *AI = dyn_cast<AllocaInst>(I))
    //if(isa<AllocaInst>(I))
    //{
    //  stackArrays++;
      //allocaRealSize += (unsigned)(AI->getArraySize());
      //errs() << "  Allocas: " << BB->getParent()->getName().str() 
      //       << "[" << BB->getName().str() << "]\n";
    //}
    
    // deal with calls
    CallSite cs(cast<Value>(I));

    if(!cs) continue;

    Function* callee = cs.getCalledFunction();
    if(callee == NULL)
    {
      attr->indirectCalls++;
    }
    else
    {
      // we'd rather break if we set cannotInline, but then we
      // wouldn't count all the instructions. We continue, but
      // check for cannotInline above

      // can't inline functions with setjump
      if (callee->isDeclaration() && 
          (callee->getName() == "setjmp" || callee->getName() == "_setjmp"))
      {
        attr->cannotInline = true;
        continue;
      }
      
      // squash any inlining of immediately-recursive functions
      if(callee == cs.getCaller())
      {
        attr->cannotInline = true;
        continue;
      }
      
      if(callee->isDeclaration())
      {
        attr->externCalls++;
        continue;
      }
      
      // if we made it here, must be a direct call
      attr->directCalls++;
    } // call instruction
  } // for instructions


  // include this block in the size of the function
  if(attr != NULL)
  {
    attr->size += size;
    if(BB->getTerminator()->getNumSuccessors() > 1)
      attr->branches++;
  }
  
  return(size);
}


// assumes that evalMetric has been kept up-to-date
int CPCallRecord::inlineSize()
{
  Function* callee = cs.getCalledFunction();
  if(callee == NULL)
    return(0);

  FunctionAttr* attr = getFunctionAttr(callee);
  unsigned funcSize = attr->size;
  unsigned less = totalImpact.instrRemIfConst + totalImpact.instrRemIfAlloca;
  
  //errs() << "  Callee has " << attr->branches << " branches.\n";
  if(attr->branches == 0)
  {
  //  errs() << "  Callee has no branches\n";
    less += inlineWeights::oneblock;
  }

  int isize = funcSize - less;

  //errs() << "CPCallRecord::inlineSize: ";
  //print(errs());
  //errs() << funcSize << " - " << less << " = " << isize << "\n";

  return(isize);
}



//===================================================================//
//                                                                   //
//      METRICS                                                      //
//                                                                   //
//===================================================================//

void CPCallRecord::initMetricMap()
{
  // static metrics
  _metricmap["null"] = &nullMetric;
  _metricmap["never"] = &neverMetric;
  _metricmap["anti"] = &antiMetric;
  _metricmap["benefit"] = &benefitMetric;

  // simple point metrics
  _metricmap["mean"] = &meanMetric;
  _metricmap["min"]  = &minMetric;
  _metricmap["max"]  = &maxMetric;

  //distribution point metrics
  _metricmap["QPoint"]   = &QPointLinearMetric;  // alias for QPLinear
  _metricmap["QPLinear"] = &QPointLinearMetric;
  _metricmap["QPSqrt"]   = &QPointSqrtMetric;

  //distribution range metrics
  _metricmap["QRange"]   = &QRangeLinearMetric;  // alias for QRLinear
  _metricmap["QRLinear"] = &QRangeLinearMetric;
  _metricmap["QRSqrt"]   = &QRangeSqrtMetric;
}


bool CPCallRecord::selectMetric(const std::string& name)
{
  if(_metricmap.size() == 0)
    CPCallRecord::initMetricMap();

  errs() << "CPCallRecord::selectMetric: selecting metric " << name;

  MetricNameMap::iterator which = _metricmap.find(name);
  if(which == _metricmap.end())
  {
    errs() << "\nCPCallRecord::selectMetric: unknown metric: " << name << "\n";
    _metric = _metricmap["null"];
    return(false);
  }
  
  // for metrics using quantiles, sanity check FDIQList
  if(name[0] == 'Q')
  {
    unsigned numQs = FDIQList.size();
    switch(name[1])
    {
    case 'P':  // need at least 1 Q value
      if(numQs < 1)
      {
        errs() << "CPCallRecord::selectMetric: No Q given for point metric\n";
        return(false);
      }
      break;
    case 'R':  // need at least 1 pair of Q values
      if(numQs < 2)
      {
        errs() << "CPCallRecord::selectMetric: Need 2 Qs for a range metric\n";
        return(false);
      }
      if(numQs%2 != 0)
      {
        errs() << "CPCallRecord::selectMetric: Odd number of Qs for range metric\n";
        return(false);
      }
      break;
    default:  // can't happen by design; metric names are all QP... or QR...
      errs() << "CPCallRecord::selectMetric Broken Q metric name\n";
      return(false);
      break;
    }
    
    errs() << " Q= ";

    // check the values too...
    for(unsigned i = 0; i < numQs; ++i)
    {
      double q = FDIQList[i];
      // Assume that values >1 are percents, 
      // eg, 50 (/100) instead of 0.5 (/1.0)
      // Make sure it's all in the standard 0.5 form
      if(q > 1)
      {
        q = q/100;
        FDIQList[i] = q;
      }

      if( (q < 0) || (q > 1) )
      {
        errs() << "CPCallRecord::selectMetric: Q[" << i 
               << "] out of range [0,1]: " << q << "\n";
        return(false);
      }
      errs() << " " << format("%.3f", FDIQList[i]);
    }
  }

  _metric = which->second;
  errs() << " (ok)\n";
  return(true);
}


double CPCallRecord::evalMetric()
{
  if(_metric == NULL)
  {
    errs() << "CPCallRecord::metricEval: Error: No metric selected!\n";
    mval = -1;
    return(-1);
  }

  // debug
  //print(errs());
  //errs() << "\n";
  //cphist->print(errs());

  
  totalImpact = ZeroArgImpact;

  CallSite::arg_iterator argIter = cs.arg_begin();
  Function* callee = cs.getCalledFunction();

  if(cs.arg_size() != callee->arg_size())
    errs() << "CPCallRecord::inlineBenefit Error: arg count mismatch: call: " 
           << cs.arg_size() << " callee: " << callee->arg_size() << "\n";

  // Aggregate the impact of the characteristics of the actual parameters
  for(unsigned argNum = 0; argNum < cs.arg_size(); ++argNum, ++argIter)
  {
    // skip if actual parameter is not a constant or pointer
    if( !(isa<Constant>(argIter)) && !(isa<AllocaInst>(argIter)) )
      continue;
    
    // Multiple constant args will interact in unpredictable ways.
    // ... but llvm's arg impact estimation is terrible, so just add
    ArgImpact* impact = getArgImpact(callee, argNum);
    if(isa<Constant>(argIter))
    {
      //if(impact->instrRemIfConst > totalImpact.instrRemIfConst) 
      totalImpact.instrRemIfConst += impact->instrRemIfConst;
      
      // PB: args are only considered one at a time, so a branch will
      //only resolve due to a constant param if it depends on only one
      //param.  
      totalImpact.branchRemIfConst += impact->branchRemIfConst;
      
      // PB: an icall can only resolve to one address, thus cummulative
      //if(impact->icallRemIfConst > totalImpact.icallRemIfConst) 
      totalImpact.icallRemIfConst += impact->icallRemIfConst;        
    }
    
    // impact of not passing a pointer; cummulative for multiple args
    if(isa<AllocaInst>(argIter))
      totalImpact.instrRemIfAlloca += impact->instrRemIfAlloca;        
  }

  
  double benefit = inlineBenefit();
  double cost = inlineCost();

  // can't get improvement from negative benefits without negative costs
  if( (cost >= 0) && (benefit <= 0) )
  {
    mval = -1;
  }
  else  // apply the selected metric
  {
    mval = (*_metric)(*this, benefit);
    
    //if(cost == 0) leave mval unmodified.
    if(cost > 0)
    {
      // normalize to cost
      mval = mval / cost;
    }
    else if(cost < 0)
    {
      // bonus mval for negative cost
      //mval = mval - cost;
      mval = mval * (-cost);
    }
  }

  errs() << "mval(" << format("%.2f", benefit) << ", " << format("%.2f", cost) 
         << ") = " << format("%.2f", mval) << "\n";
  
  return(mval);
}



// Per-call (dynamic) benefit of inlining (mostly instructions saved).
// The caller should weight this benefit by, eg., expected frequency,
// as appropriate.  Benefit is determined by:
// - constant parameters
// - locals passed by pointer
// - setup/return overhead
double CPCallRecord::inlineBenefit()
{
  // start off with the savings for call/return overhead
  unsigned benefit = inlineWeights::callReturn;

  // estimate instructions saved due to arg characteristics
  benefit += totalImpact.instrRemIfConst  * inlineWeights::instr;
  benefit += totalImpact.branchRemIfConst * inlineWeights::branch;
  benefit += totalImpact.icallRemIfConst  * inlineWeights::icall;
  benefit += totalImpact.instrRemIfAlloca * inlineWeights::alloca;
  
  // LLVM says each arg is worth about 1 instruction
  benefit += cs.arg_size();

  // Give a small bonus for inlining icalls; they might be resolved in
  // subsequent inlining
  FunctionAttr* attr = getFunctionAttr(cs.getCalledFunction());
  benefit += attr->indirectCalls;

  return(benefit);
}


double CPCallRecord::inlineCost()
{
  //unsigned cost = 0;

  //cost += getFunctionAttr(cs.getCalledFunction())->allocaSize;

  //unsigned size = inlineSize();
  //if(size > 0) cost += size;

  //return(cost);
  return(inlineSize());
}



double CPCallRecord::nullMetric(CPCallRecord& rec, double benefit) 
{ 
  return(0.0); 
}

double CPCallRecord::benefitMetric(CPCallRecord& rec, double benefit)
{
  return(benefit);
}

double CPCallRecord::neverMetric(CPCallRecord& rec, double benefit) 
{ 
  return(-1.0); 
}

// try to do the worst possible inlining: do the least benefit with
// the largest size first
double CPCallRecord::antiMetric(CPCallRecord& rec, double benefit)
{
  // reverse relative ordering of benefit
  double newBenefit = 1.0e6 - benefit;
  // change the benefit/size calculation to benefit*size
  unsigned size = rec.inlineSize();
  newBenefit = newBenefit * size * size;

  return(newBenefit);
}

double CPCallRecord::meanMetric(CPCallRecord& rec, double benefit)
{
  double mean = rec.cphist->mean();
  double coverage = rec.cphist->coverage();
  return(mean * benefit * coverage);
}

double CPCallRecord::maxMetric(CPCallRecord& rec, double benefit)
{
  return(rec.cphist->max()*benefit);
}

double CPCallRecord::minMetric(CPCallRecord& rec, double benefit)
{
  double min = rec.cphist->min();
  double coverage = rec.cphist->coverage();
  return(min * benefit * coverage);
}


double CPCallRecord::QPointLinearMetric(CPCallRecord& rec, double benefit)
{
  double rc = 0;

  errs() << "Applying QPointLinearMetric (" << format("%.2f", benefit) 
         << ") with " << FDIQList.size() << " q points\n";

  if(FDIQList.size() == 0)
    errs() << "CPCallRecord: no points for Q metric\n";

  for(unsigned i = 0, E = FDIQList.size(); i < E; ++i)
  {
    double v = rec.cphist->quantile(FDIQList[i]);
    errs() << "v[" << format("%.2f", FDIQList[i]) << "] = " 
           << format("%.4f", v) << "\n";
    rc += v*benefit;
  }

  return(rc);
}


double CPCallRecord::QPointSqrtMetric(CPCallRecord& rec, double benefit)
{
  double rc = 0;

  errs() << "Applying QPointSqrtMetric (" << format("%.2f", benefit) 
         << ") with " << FDIQList.size() << " q points\n";

  if(FDIQList.size() == 0)
    errs() << "CPCallRecord: no points for Q metric\n";

  for(unsigned i = 0, E = FDIQList.size(); i < E; ++i)
  {
    double v = rec.cphist->quantile(FDIQList[i]);
    errs() << "v[" << format("%.2f", FDIQList[i]) << "] = " 
           << format("%.4f", v) << "\n";
    rc += sqrt(v*benefit);
  }

  return(rc);
}


double CPCallRecord::QRangeLinearMetric(CPCallRecord& rec, double benefit)
{
  double rc = 0;

  errs() << "Applying QRangeLinearMetric (" << format("%.2f", benefit) 
         << ") with " << FDIQList.size() << " q points\n";

  if(FDIQList.size() == 0)
    errs() << "CPCallRecord: no points for Q metric\n";

  for(unsigned i = 0, E = FDIQList.size(); i < E; i+=2)
  {
    double lowQ = FDIQList[i];
    double highQ = FDIQList[i+1];
    double v = rec.cphist->applyOnQuantile(lowQ, highQ, &CPHistogram::product);
    errs() << "v[" << format("%.2f", lowQ) << ", " << format("%.2f", highQ) 
           << "] = " << format("%.4f", v) << "\n";
    rc += v*benefit;
  }

  return(rc);
}


double CPCallRecord::QRangeSqrtMetric(CPCallRecord& rec, double benefit)
{
  double rc = 0;

  errs() << "Applying QRangeSqrtMetric (" << format("%.2f", benefit) 
         << ") with " << FDIQList.size() << " q points\n";

  if(FDIQList.size() == 0)
    errs() << "CPCallRecord: no points for Q metric\n";

  for(unsigned i = 0, E = FDIQList.size(); i < E; i+=2)
  {
    double lowQ = FDIQList[i];
    double highQ = FDIQList[i+1];
    double v = rec.cphist->applyOnQuantile(lowQ, highQ, &CPHistogram::product);
    errs() << "v[" << format("%.2f", lowQ) << ", " << format("%.2f", highQ) 
           << "] = " << format("%.4f", v) << "\n";
    rc += sqrt(v*benefit);
  }

  return(rc);
}
