//===- CombinedPathProfile.cpp --------------------------------*- C++ -*---===//
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
#define DEBUG_TYPE "cp-histogram"

#include "llvm/Analysis/ProfileInfoTypes.h"
#include "llvm/Analysis/CombinedProfile.h"
#include "llvm/Analysis/CPHistogram.h"
#include "llvm/Analysis/PathNumbering.h"
#include "llvm/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <stdlib.h>


using namespace llvm;

// ----------------------------------------------------------------------------
// Combined path profile implementation
// ----------------------------------------------------------------------------


// PB: could probably make _fuinctionRef a class variable and only do
// this once, but will we need to worry about instances that use
// different modules?
CombinedPathProfile::CombinedPathProfile(Module& module) 
{
  for( Module::iterator F = module.begin(), E = module.end();
       F != E; ++F )
    if( !F->isDeclaration() )
      _functionRef.push_back(F);
}


unsigned CombinedPathProfile::serialize(FILE* f) {
	// Write the CPP header
  ProfilingType ptype = CombinedPathInfo;
  unsigned psize = _functions.size();
	if( (fwrite(&ptype, sizeof(unsigned), 1, f) != 1) ||
      (fwrite(&_weight, sizeof(double), 1, f) != 1) ||
      (fwrite(&psize, sizeof(unsigned), 1, f) != 1) ||
      (fwrite(&_bincount, sizeof(unsigned), 1, f) != 1) ) 
  {
		errs() << "error: unable to write CPP to file.\n";
		return(0);
	}
  
  unsigned written = 0;
	// Iterate through each function to write it
	for( CPPFunctionMap::iterator F = _functions.begin(), E = _functions.end();
			F != E; ++F ) {
		// Write the function header
		PathHeader ph = { F->first, F->second.size() };
		if( fwrite(&ph, sizeof(PathHeader), 1, f) != 1 ) {
			errs() <<
			  "error: unable to write CPP histogram function header to file.\n";
			return(0);
		}

		// Iterate through each executed path in the function
		for( CPPHistogramMap::iterator H = F->second.begin(), HE = F->second.end();
				H != HE; ++H ) 
    {
      CPHistogram* hist = _histograms[H->second];
      //if( !H->second->serialize(H->first, f) )
      if( !hist->serialize(H->first, f) )
      {
        errs() << "error: CPP::serialize failed to serialize histogram: f:" 
               << F->first << ", p:" << H->first << " @" << H->second << "\n";
        return(0);
      }
      written++;
		}
	}
  return(written);
}

bool CombinedPathProfile::deserialize(FILE* f) {
	unsigned funcCount;

	if( !fread(&_weight, sizeof(double), 1, f) ||
		  !fread(&funcCount, sizeof(unsigned), 1, f) ||
		  !fread(&_bincount, sizeof(unsigned), 1, f) ) {
		errs() << "warning: combined path profiling data corrupt.\n";
		return false;
	}

	DEBUG(dbgs() << "Function Count: " << funcCount << "\n");
	DEBUG(dbgs() << "Bin Count:      " << _bincount << "\n");

  unsigned histIndex = 0;  // index of next new histogram

	// Read in each function
	while( funcCount-- ) {
		// Get the function header
		PathHeader ph;
		if( fread(&ph, sizeof(PathHeader), 1, f) != 1 ) {
			errs() <<
				"CPP::deserialize Error: failed to read path header\n";
			return false;
		}

		// Read in each path
		while( ph.numEntries-- ) 
    {
      CPHistogram* hist = new CPHistogram();

      int pathnum = hist->deserialize(_bincount, _weight, f);
      if(pathnum == -1)
      {
        errs() << "CPP::deserialize Error: failed to read histogram\n";
        delete hist;
        return(false);
      }

      // PB should we check if we're replacing an existing histogram?
      _histograms.push_back(hist);
			_functions[ph.fnNumber][pathnum] = histIndex++;
		}
	}

	return true;
}


// Read in a standard path profile and add the frequencies to the add
// lists of the corresponding histograms.  Requires the number of bins
// to use (binCount).
bool CombinedPathProfile::addProfile(FILE* f)
{

  //errs() << "--> addPathProfile\n";

  // get the number of functions in this profile
  unsigned functionCount;
  if( fread(&functionCount, sizeof(unsigned), 1, f) != 1 ) 
  {
    errs() << "  error: path profiling info has no header\n";
    return false;
  }

  //errs() << "  " << functionCount << " path function(s) identified.\n";

  addWeight(1.0);

  // Iterate through each function
  for(unsigned i = 0; i < functionCount; ++i) 
  {
    //errs() << "  Function " << i << " of " << functionCount << "\n";
    PathHeader functionHeader;
    if( fread(&functionHeader, sizeof(PathHeader), 1, f) != 1 ) 
    {
      errs() << "  error: bad path profiling file syntax\n";
      return(false);
    }
    FunctionIndex funcNum = functionHeader.fnNumber;

    // Build a DAG for the function
    //errs() << "    build dag\n";
    BallLarusDag dag(*_functionRef[funcNum-1]);
    //errs() << "    init dag\n";
    dag.init();
    //errs() << "    calc path numbers\n";
    dag.calculatePathNumbers();
    
    //setCurrentFunction(funcNum);
    unsigned totalNumberExecuted = 0;
    std::list<PathTableEntry> newPaths;
    
    //errs() << "    Iterate paths\n";

    // Iterate through each path entry, and add it
    for(unsigned ii = 0; ii < functionHeader.numEntries; ++ii ) 
    {
      //errs() << "      Path " << ii << "\n";
      PathTableEntry pte;
      if( fread(&pte, sizeof(PathTableEntry), 1, f) != 1 ) 
      {
        errs() << "  error: bad path profiling file syntax\n";
        return(false);
      }
      newPaths.push_back(pte);

      BallLarusEdge* edge = dag.getFirstBLEdge(pte.pathNumber);
      if( edge->getType() == BallLarusEdge::NORMAL 
          && totalNumberExecuted < 0xffffffff ) 
      {
        //errs() << "Path #" << pte.pathNumber << " is normal!\n";
        totalNumberExecuted += pte.pathCounter;
      }
    }
    
    //errs() << "    done iterating paths.  Total: " 
    //       << totalNumberExecuted << "\n";

    for( std::list<PathTableEntry>::iterator P = newPaths.begin(),
           E = newPaths.end(); P != E; ++P )
    {
      //errs() << "    Path: " << P->pathNumber 
      //       << " Counter: " << P->pathCounter << "\n";
      if(P->pathCounter > 0)
      {
        double pathFreq = double(P->pathCounter)/totalNumberExecuted;
        CPHistogram& hist = getHistogram(funcNum, P->pathNumber);
        hist.addToList(pathFreq);
      }
    }
  }
  //errs() << "<-- addPathProfile\n";
  return(true);
}

/*
unsigned CombinedPathProfile::calcBinCount(CPPList& list, unsigned fallback)
{
  // nothing in list: use the default
  if(list.size() == 0) return(fallback);

  // when there is only one CP, just use it's original bincount
  if(list.size() == 1) return(list.front()->getBinCount());

  // find the largest bincount in the list
  unsigned bins = 1;
  for(CPPList::iterator i = list.begin(), E = list.end(); i != E; ++i)
    if( (*i != NULL) && ((*i)->getBinCount() > bins) ) 
      bins = (*i)->getBinCount();
  return(bins);
}
*/

// Even though list is a generic CPList, it should only contain CPPs
bool CombinedPathProfile::buildFromList(CPList& list, unsigned binCount) 
{
	if(list.size() == 0)
		return true;

  ProfilingType myType = getProfilingType();

  if(binCount == 0)
    _bincount = calcBinCount(list);
  else
    _bincount = binCount;

	// Update the trial count
	for(CPList::iterator CP = list.begin(), E = list.end(); CP != E; ++CP)
  {
    if((*CP)->getProfilingType() != myType)
    {
      errs() << "CPP::buildFromList Warning: CP in list is not a CPP\n";
      continue;
    }

    CombinedPathProfile* cp = (CombinedPathProfile*)(*CP);
		_weight += cp->_weight;
  }

	// Iterate through all the potential functions in the program and
	// collect all the histograms for each path from all CPs in the list
	for(unsigned funcID = 0, S = _functionRef.size(); funcID < S; ++funcID) 
  {
		// Function path combined profiling histogram map
    // pathNumber --> CPHistogramList
		std::map<unsigned,CPHistogramList> fpcphm;

		// Iterate through the list of profiles
		for( CPList::iterator CP = list.begin(), E = list.end(); CP != E; ++CP) 
    {
      if((*CP)->getProfilingType() != myType)
        continue;

      CombinedPathProfile* cp = (CombinedPathProfile*)(*CP);
			// Iterate through paths of this function in this profile
			for( CPPHistogramMap::iterator H = cp->_functions[funcID].begin(),
					HE = cp->_functions[funcID].end(); H != HE; ++H ) 
      {
        // add this CPs histogram for this path to the list for this path
        unsigned pathnum = H->first;
        CPHistogram& hist = cp->getHistogram(funcID, pathnum);
				fpcphm[pathnum].push_back(&hist);
			}
		}

    // Now: fpcphm maps each path in this function to the list of its
    // histograms from all profiles

    // build a single merged CP from the collected list for each path
    unsigned histIndex = _histograms.size();  // index of first new histogram
		for( std::map<unsigned,CPHistogramList>::iterator H = fpcphm.begin(),
				E = fpcphm.end(); H != E; ++H ) 
    {
      CPHistogram* hist = new CPHistogram(_bincount, _weight, H->second);
      _histograms.push_back(hist);
			_functions[funcID][H->first] = histIndex++; 
		}
	}

	return true;
}


 /*  // MIGHT STILL WANT for the by-function printing
void CombinedPathProfile::print(llvm::raw_ostream& stream) {
	stream << "Total Weight: " << _weight << "\n";
	stream << "Bin Count:   " << _bincount << "\n";

  // Iterate through each function
	for( CPPFunctionMap::iterator F = _functions.begin(),
			E = _functions.end(); F != E; ++F ) 
  {
		outs() << "----- Function #" << F->first << " -----\n";
		// Iterate through each histogram in the function
		for( CPPHistogramMap::iterator H = F->second.begin(),
				E = F->second.end(); H != E; ++H ) 
    {
			stream << "Path #" << H->first << ":\n";
      _histograms[H->second]->print(stream);
		}
	}
}
 */

 /*
void CombinedPathProfile::printHistogramInfo(llvm::raw_ostream& stream)
{
  stream << "#pathID\tmin\tmax\tused\tmean\tstdev\tweight\tmaxW\tdomDepth\n";
  // Iterate through each function
	for( CPPFunctionMap::iterator F = _functions.begin(),
			E = _functions.end(); F != E; ++F ) 
  {
		// Iterate through each histogram in the function
		for( CPPHistogramMap::iterator H = F->second.begin(),
				E = F->second.end(); H != E; ++H ) 
    {
      CPHistogram& h = *(_histograms[H->second]);
      if(h.nonZero())
      {
        // funcID-pathID min max used% mean stdev weight% maxW% domDepth
        stream << F->first << "-" << H->first << "\t"
               << h.min() << "\t" << h.max() << "\t"
               << (double)h.getBinsUsed()/(double)h.bins() << "\t" 
               << h.mean() << "\t" << h.stdev() << "\t" 
               << h.nonZeroWeight()/h.totalWeight() << "\t"
               << h.maxWeight()/h.totalWeight() << "\t0\n";
      }
		}
	}
}
 */

  /*
void CombinedPathProfile::printHistogramStats(llvm::raw_ostream& stream)
{
  stream << "#ID\tP/H\tPval\tOcc\tCov\tML\tSpan\n";
  // Iterate through each function
	for( CPPFunctionMap::iterator F = _functions.begin(),
			E = _functions.end(); F != E; ++F ) 
  {
		// Iterate through each histogram in the function
		for( CPPHistogramMap::iterator H = F->second.begin(),
				E = F->second.end(); H != E; ++H ) 
    {
      CPHistogram& hist = *(_histograms[H->second]);
      if(hist.nonZero())
      {
        // funcID-pathID P/H Pval Occupancy Coverage MaxLikelyhood Span
        stream << F->first << "-" << H->first << "\t";
        hist.printStats(stream);
        stream << "\n";
      }
		}
	}
}
  */

unsigned CombinedPathProfile::getFunctionCount() const {
	return _functions.size();
}


// check if a PathID is valid, ie, the function and path already exist
// in the _functions map.
bool CombinedPathProfile::valid(const PathID& path) const
{
  FunctionIndex f = path.first;
  PathIndex p = path.second;

  if(_functions.count(f) > 0)
    if(_functions.find(f)->second.count(p) > 0)
      return(true);
  
  return(false);
}

CPHistogram& CombinedPathProfile::getHistogram(const FunctionIndex funcIndex, 
                                               const PathIndex pathIndex)
{
  CPPHistogramMap& funcPaths = _functions[funcIndex];
  unsigned histIndex = funcPaths[pathIndex];
  if(histIndex+1 > _histograms.size())
    _histograms.resize(histIndex+1);
  CPHistogram* hist = _histograms[funcPaths[pathIndex]];
  if(hist == NULL)
  {
    unsigned histIndex = _histograms.size();
    hist = new CPHistogram();
    _histograms.push_back(hist);
    funcPaths[pathIndex] = histIndex;
  }
  return(*hist);
}


CPHistogram& CombinedPathProfile::getHistogram(const PathID& path)
{
  return(getHistogram(path.first, path.second));
}


CPHistogram& CombinedPathProfile::operator[](const PathID& path)
{
  return(getHistogram(path));
}

void CombinedPathProfile::getPathSet(PathSet& paths) const
{
  // Iterate through each function
	for(CPPFunctionMap::const_iterator F = _functions.begin(),
         E = _functions.end(); F != E; ++F ) 
  {
		// Iterate through each path in the function
		for( CPPHistogramMap::const_iterator H = F->second.begin(),
           E = F->second.end(); H != E; ++H ) 
    {
      PathID pid = PathID(F->first, H->first);
      paths.insert(pid);
		}
	}
}


// Path needs it's own implementation, because _histogram indexes are
// not consistent across profiles.
void CombinedPathProfile::printDrift(CombinedPathProfile& other, 
                                     llvm::raw_ostream& stream)
{
  PathSet ps;

  getPathSet(ps);
  other.getPathSet(ps);

  stream << "#pathID\t0-out\t0-in\n";
  for(PathSet::iterator p=ps.begin(), E=ps.end(); p != E; ++p)
  {
    if(!valid(*p) || !other.valid(*p))
    {
      // if path only exists in one profile, then 0% overlap
      errs() << "warning: path exists in only 1 profile: " 
             << p->first << "-" << p->second << "\n";
      continue;
    }

    CPHistogram& h1 = getHistogram(*p);
    CPHistogram& h2 = other.getHistogram(*p);

    if( h1.isPoint() && h2.isPoint() )
      continue;

    stream << p->first << "-" << p->second << "\t" 
           << 1-h1.overlap(h2, false) << "\t" 
           << 1-h1.overlap(h2, true) << "\n";
  }

}

