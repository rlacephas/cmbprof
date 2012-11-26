//===- CombinedProfile.cpp ------------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  CPHistogram, CombinedEdgeProfile, and CombinedPathProfile each
//  have individual cpp files.
//
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "combined-profile"

#include "llvm/Analysis/ProfileInfoTypes.h"
#include "llvm/Analysis/CombinedProfile.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;


// ----------------------------------------------------------------------------
// Combined profile implementation
// ----------------------------------------------------------------------------

CombinedProfile::CombinedProfile() : _weight(0) {
}

CombinedProfile::~CombinedProfile()
{
  //errs() << "Freeing histograms\n";
  for(unsigned i = 0, E = _histograms.size(); i != E; ++i)
    if(_histograms[i] != NULL)
      delete (_histograms[i]);
}

unsigned CombinedProfile::getBinCount() const {
	return _bincount;
}

double CombinedProfile::getTotalWeight() const {
	return _weight;
}

void CombinedProfile::addWeight(double w) {
	_weight += w;
  //errs() << "addWeight: " << _weight << "\n";
}


void CombinedProfile::buildHistograms(unsigned binCount)
{
	_bincount = binCount;

  for(unsigned i = 0, E = _histograms.size(); i != E; ++i)
  {
    if(_histograms[i] != NULL)
      _histograms[i]->buildFromList(_bincount, _weight);
  }
}


void CombinedProfile::print(llvm::raw_ostream& stream)
{
  int binsUsed = 0;

  stream << "Profile Type: " << getNameStr() << "\n";
	stream << "Total Weight: " << _weight << "\n";
	stream << "Bin Count:    " << _bincount << "\n";

	for( unsigned i = 0, E = _histograms.size(); i < E; i++ ) 
  {
    stream << "\nIndex " << i << ":\n";
    _histograms[i]->print(stream);
    binsUsed += _histograms[i]->getBinsUsed();
  }
  stream << " ** Total Histogram Bins Used: " << binsUsed << "\n";
}


void CombinedProfile::printHistogramInfo(llvm::raw_ostream& stream)
{

  if(_histograms.size() == 0)
    errs() << "Warning: no histograms\n";

  stream << "#" << getNameStr() << "Index\tmin\tmax\tused\tmean\tstdev\tweight\tmaxW\n";
  for(unsigned i = 0, E = _histograms.size(); i < E; i++)
  {
    CPHistogram* h = _histograms[i];

    if( (h != NULL) && h->nonZero() )
    {
      // index min max used% mean stdev weight% maxW%
      stream << i << "\t"
             << h->min() << "\t" << h->max() << "\t"
             << (double)h->getBinsUsed()/(double)h->bins() << "\t" 
             << h->mean() << "\t" << h->stdev() << "\t" 
             << h->nonZeroWeight()/h->totalWeight() << "\t"
             << h->maxWeight()/h->totalWeight() << "\t";
		}
	}
}


void CombinedProfile::printHistogramStats(llvm::raw_ostream& stream)
{
  if(_histograms.size() == 0)
    errs() << "Warning: no histograms\n";

  stream << "#" << getNameStr() << "Index\tP/H\tPval\tOcc\tCov\tML\tSpan\temdU\temdN\n";
  for(unsigned i = 0,  E = _histograms.size(); i < E;  i++)
  {
    CPHistogram* h = _histograms[i];

    if( (h != NULL) && h->nonZero() )
    {
      // index  P/H  Pval  Occ  Cov  ML  Span
      stream << i << "\t";
      h->printStats(stream);
      stream << "\n";
		}
	}
}


void CombinedProfile::printSummary(llvm::raw_ostream& stream)
{
  int items = 0;
  int zero = 0;
  int peq1cov1 = 0;   // point == 1, 100% coverage
  int pneq1cov1 = 0;  // point != 1, 100% coverage
  int peq1 = 0;       // point == 1, <100% coverage
  int pneq1 = 0;      // point != 1, <100% coverage
  int histcov1 = 0;   // histogram, 100% coverage
  int hist = 0;       // histogram, <100% coverage

  if(_histograms.size() == 0)
    errs() << "Warning: no histograms\n";

  for(unsigned i = 0, E = _histograms.size(); i < E; i++)
  {
    CPHistogram* h = _histograms[i];
    
    if( (h == NULL) || (!h->nonZero()) )
    {
      zero++;
      continue;
    }
    
    items++;
    
    if(h->isPoint())
    {
      if(h->min() == 1.0)
      {
        if(h->coverage() > (1.0-1.0e-10))
          peq1cov1++;
        else
          peq1++;
      }
      else // point not at 1
      {
        if(h->coverage() > (1.0-1.0e-10))
          pneq1cov1++;
        else
          pneq1++;
      }
    }
    else // non-point
    {
      if(h->coverage() > (1.0-1.0e-10))
        histcov1++;
      else
        hist++;
    }
    
  }

  /*  // fully-decomposed summary: too many columns :( 
  stream << edges << " & "
         << hist << " & " << histcov1 << " && " 
         << pneq1 << " & " << pneq1cov1 << " && " 
         << peq1 << " & " << peq1cov1 << "\n";
  */

  stream << items << " & "
         << (hist+histcov1)*100/items << " & " 
         << hist << " & " << histcov1 << " && " 
         << (pneq1+pneq1cov1)*100/items << " & " 
         << (peq1+peq1cov1)*100/items << "\n";

}


void CombinedProfile::printDrift(const CombinedProfile& other, 
                                 llvm::raw_ostream& stream) const
{

  // build union of non-zero histograms
  IndexSet I;

  for(unsigned i = 0, E = size(); i < E; ++i)
    if( (_histograms[i] != NULL) && _histograms[i]->nonZero() )
      I.insert(i);

  for(unsigned i = 0, E = other.size(); i < E; ++i)
    if( (other._histograms[i] != NULL) && other._histograms[i]->nonZero() )
      I.insert(i);

  if(I.size() == 0)
    errs() << "Warning: no histograms\n";

  // Compute and print drift
  stream << "#" << getNameStr() << "Index\t0-out\t0-in\n";
  for(IndexSet::iterator i = I.begin(), E = I.end(); i != E; ++i)
  {
    // check for 0-overlap (100% drift) cases
    if( (*i > size()) || (*i > other.size()) 
        || (_histograms[*i] == NULL) || (other._histograms[*i] == NULL) 
        || !_histograms[*i]->nonZero() || !other._histograms[*i]->nonZero() )
    {
      errs() << "Warning: histogram " << *i << " only exists in one profile!\n";
      stream << *i << "\t1.0\t1.0\n"; 
      continue;
    }

    CPHistogram* h1 = _histograms[*i];
    CPHistogram* h2 = other._histograms[*i];

    if( h1->isPoint() && h2->isPoint() && (h1->min() != h2->min()))
    {
      errs() << "Warning: histogram " << *i << " has different point values\n";
      stream << *i << "\t1.0\t1.0\n";
      continue;
    }

    // finally, no exceptional situations!
    stream << *i << "\t" << 1-h1->overlap(*h2, false) << "\t" 
           << 1-h1->overlap(*h2, true) << "\n";
    
  }
}


unsigned CombinedProfile::calcBinCount(CPList& list, unsigned fallback) const
{
  bool valid = false;
  ProfilingType ptype = getProfilingType();

  // nothing in list: use the default
  if(list.size() == 0) return(fallback);

  // find the largest bincount in the list
  unsigned bins = 1;
  for(CPList::iterator i = list.begin(), E = list.end(); i != E; ++i)
  {
    if( (*i != NULL)
        && ((ptype == 0) || ((*i)->getProfilingType() == ptype))
        && ((*i)->getBinCount() > bins) )
    {
      bins = (*i)->getBinCount();
      valid = true;
    }
  }
  
  if(valid)
    return(bins);
  else
    return(fallback);
}
