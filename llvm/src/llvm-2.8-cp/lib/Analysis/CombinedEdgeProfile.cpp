//===- CombinedEdgeProfile.cpp --------------------------------*- C++ -*---===//
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
#include "llvm/Analysis/EdgeDominatorTree.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <stdlib.h>


using namespace llvm;

// ----------------------------------------------------------------------------
// Combined edge profile implementation
// ----------------------------------------------------------------------------

// _edt for all
EdgeDominatorTree* CombinedEdgeProfile::_edt = NULL;


CombinedEdgeProfile::CombinedEdgeProfile(Module& module) 
{
  if(_edt == NULL)
    _edt = new EdgeDominatorTree(module);
  _histograms.resize(_edt->getEdgeCount());
}

void CombinedEdgeProfile::freeStaticData()
{
  if(_edt != NULL)
    delete _edt;
}


// Read in a standard edge profile and add the
// hierarchically-normalized frequencies to the add lists of the
// corresponding histograms.  Requires the number of bins to use
// (binCount).
bool CombinedEdgeProfile::addProfile(FILE* file)
{
  
  if(_edt == NULL)
  {
    errs() << "addEdgeProfile: error: EDT not set!\n";
    return(false);
  }

  //errs() << "--> addEdgeProfile\n";
  
  // get the number of edges in this profile
  unsigned edgeCount;
  if( fread(&edgeCount, sizeof(edgeCount), 1, file) != 1 ) {
    errs() << "  error: edge profiling info has no header\n";
    return(false);
  }
  if(_histograms.size() != edgeCount) 
  {
    if(_histograms.size() != 0)
      errs() << "CEP::addProfile: warning: edge count has changed from " << _histograms.size() << " to " << edgeCount << "\n";
    _histograms.resize(edgeCount);
  }
  //errs() << "CEP::addProfile: " << edgeCount << " edges\n";

  // TODO: is edge count reasonable for this program?
  // Also ... do all of the edge profiles have the proper edge count?
  // Compare it to the dominator tree, since that information will be there
  
  unsigned* edgeBuffer = new unsigned[edgeCount];
  if( fread(edgeBuffer, sizeof(unsigned), edgeCount, file)
      != edgeCount) {
    delete [] edgeBuffer;
    errs() << "  warning: edge profiling info header/data mismatch\n";
    return(false);
  }

  addWeight(1.0);

  for( unsigned i = 0; i < edgeCount; i++ ) {
    // Add a new histogram entry
    double normFreq = 0;
    unsigned execCnt = edgeBuffer[i];
    unsigned domID = _edt->getDominatorIndex(i);
    unsigned domCnt = edgeBuffer[domID];

    // calculate the hierarchially-normalized frequency
    if(domID == i)
    {
      // no dominator or self-dominator: must be a root node
      // note: root normalizes to 1, even if execCnt = 0
      normFreq = 1;
    }
    else
    {
      if(domCnt == 0)  // should only happen if execCnt is also 0
      {
        normFreq = 0;
      }
      else
      {
        normFreq = double(execCnt) / double(domCnt);
      }
    }
    
    // use operator[] so that we check if the histogram is allocated
    operator[](i)->addToList(normFreq);
  }

  delete [] edgeBuffer;
  //errs() << "<-- addEdgeProfile\n";
  return(true);
}


// Write CEP to file - store only those histograms with data
unsigned CombinedEdgeProfile::serialize(FILE* f)
{
	unsigned edgeCount = 0;
	// Calculate the number of histograms which have non-zero data
	for( unsigned i = 0; i < _histograms.size(); i++ )
		if( _histograms[i]->nonZeroWeight() > FP_FUDGE_EPS )
			edgeCount++;
			
	//		errs() << "Found " << edgeCount << " non zeros!\n";

  ProfilingType ptype = CombinedEdgeInfo;
	// Output information about the profile
	if( (fwrite(&ptype, sizeof(unsigned), 1, f) != 1) ||
      (fwrite(&_weight, sizeof(double), 1, f) != 1) ||
      (fwrite(&edgeCount, sizeof(unsigned), 1, f) != 1) ||
      (fwrite(&_bincount, sizeof(unsigned), 1, f) != 1) ) 
  {
		errs() << "error: unable to write histogram to file.\n";
		return(0);
	}

  unsigned written = 0;
	for( unsigned i = 0; i < _histograms.size(); i++ ) {
		// Skip zero count histograms
		if( _histograms[i]->nonZeroWeight() < FP_FUDGE_EPS ) {
			DEBUG(dbgs() << "  skipping zero edge " << i << ".\n");
			continue;
		}
    if( !_histograms[i]->serialize(i, f) ) {
      errs() << "error: unable to write histogram to file.\n";
      return(0);
    }
    written++;
	}
  return(written);
}

bool CombinedEdgeProfile::deserialize(FILE* f) {
	unsigned edgeCount;

  //errs() << "--> CEP::deserialize\n";

	if( !fread(&_weight, sizeof(double), 1, f) ||
		  !fread(&edgeCount, sizeof(unsigned), 1, f) ||
		  !fread(&_bincount, sizeof(unsigned), 1, f) ) {
		errs() << "warning: combined edge profiling data corrupt.\n";
		return false;
	}

	DEBUG(dbgs() << "Edge Count: " << edgeCount << "\n" );
	DEBUG(dbgs() << "Bin Count:  " << _bincount << "\n" );

  if(edgeCount == 0)
    errs() << "Warning: no edges in CEP\n";

	while( edgeCount-- ) {
    CPHistogram* newHist = new CPHistogram();
    int index = newHist->deserialize(_bincount, _weight, f);
    
    if(index < 0) {
      errs() << "error: unable to read histogram\n";
      delete newHist;
      return false;
    }

    _histograms[index] = newHist;
	}

  
  // allocate any missing histograms
  for(unsigned i = 0; i < _histograms.size(); i++)
  {
    if( _histograms[i] == NULL ) 
      _histograms[i] = new CPHistogram();
  }

  //errs() << "<-- CEP::BuildFromFile\n";
	return true;
}


// allocates all entries in _histograms
// even though list is a generic CPList, it should only contain CEPs
bool CombinedEdgeProfile::buildFromList(CPList& list, unsigned binCount) 
{
  ProfilingType myType = getProfilingType();

  if(binCount == 0)
    _bincount = calcBinCount(list);
  else
    _bincount = binCount;

  _weight = 0;

	if(list.size() == 0)
		return true;
  
  //errs() << "--> CEP::BuildFromList (" << CEPs.size() << ")\n";

	unsigned edgeCount = 0;
  for(CPList::iterator l = list.begin(), E = list.end(); l != E; ++l)
  {
    if((*l)->getProfilingType() != myType) continue;
    edgeCount = (*l)->size();
    break;
  }

  // delete current contents (if any)
  for(unsigned i = 0; i < _histograms.size(); i++)
    if(_histograms[i] != NULL)
    {
      delete _histograms[i];
      _histograms[i] = NULL;
    }

  // reallocate to correct size
	_histograms.resize(edgeCount);  // fills with NULL pointers

	// Update the trial count
	for(CPList::iterator CP = list.begin(), E = list.end(); CP != E; ++CP)
  {
    if((*CP)->getProfilingType() != myType)
    {
      errs() << "CEP::buildFromList Warning: CP in list is not a CEP\n";
      continue;
    }

    CombinedEdgeProfile* cp = (CombinedEdgeProfile*)(*CP);
		addWeight(cp->_weight);
    unsigned edges = cp->size();
    if(edges != edgeCount)
      errs() << "CEP::buildFromList: edge count mismatch! " 
             << edges << " vs " << edgeCount << "\n";
  }

	// Merge each set of histograms
	for( unsigned i = 0; i < edgeCount; i++ ) 
  {
    CPHistogramList cphl;
    
    for( CPList::iterator CP = list.begin(), E = list.end();	CP != E; ++CP )
    {
      if((*CP)->getProfilingType() != myType)
        continue;

      CombinedEdgeProfile* cp = (CombinedEdgeProfile*)(*CP);
      if( (*cp)[i]->nonZero() )
        cphl.push_back((*cp)[i]);
    }
    
    _histograms[i] = new CPHistogram(_bincount, _weight, cphl);
	}

  //errs() << "<-- CEP::buildFromList\n";
	return true;
}


CPHistogram* CombinedEdgeProfile::operator[](const int index) {
  if( _histograms[index] == NULL )
    _histograms[index] = new CPHistogram();
	return _histograms[index];
}
