//===- CPHistogram.h ------------------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The histogram at the core of combined profiling.
//
//===----------------------------------------------------------------------===//

// TODO: don't include 0s in stats::sumOfSquares until needed (stdev(true))


#ifndef CPHISTOGRAM_H
#define CPHISTOGRAM_H

#include <stdio.h>

//#include "llvm/Analysis/ProfileInfoTypes.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <list>
#include <limits>
//#include <map>
//#include <set>

// FP imprecision usually results in nearly-zero values around e-312
// We probably only care about e-10 at the smallest
// --> This epsilon is conservative in both cases
#define FP_FUDGE_EPS  1.0e-100

namespace llvm {

  class CPHistogram;

  typedef double (*CPHistFunc)(double, double);

  typedef std::pair<double,double> WeightedValue;
  typedef std::vector<WeightedValue> WeightedValueVec;
  typedef std::list<WeightedValue> WeightedValueList;

	typedef std::list<CPHistogram*> CPHistogramList;

  class CPHistogram {
	public:
		CPHistogram();  // 0-bin, 0-value histogram
		//CPHistogram(unsigned bincount, double totalweight = 1.0);
    // We delete 0s from the list, hl cannot be const
		CPHistogram(unsigned bincount, double totalweight, CPHistogramList& hl);
    CPHistogram(const CPHistogram& rhs);
    ~CPHistogram();

    CPHistogram& operator=(const CPHistogram& rhs);
    CPHistogram* cross(const CPHistogram& other) const;
    CPHistogram* cross(const CPHistogramList& others) const;
    CPHistogram* asUniform() const;
    CPHistogram* asNormal() const;
    double earthMover(const CPHistogram& other) const;

    double getBinWidth() const;
    double getBinCenter(unsigned b) const;
    double getBinUpperLimit(unsigned b) const;
    double getBinLowerLimit(unsigned b) const;
    unsigned whichBin(double v) const;
    bool isPoint() const;
    unsigned bins() const;
    unsigned getBinsUsed() const;
    double getBinWeight(unsigned b) const;
    double getRangeWeight(double lb, double ub) const;
    bool nonZero() const;

    double mean(bool inclZeros=false) const;
    double stdev(bool inclZeros=false) const;
    double min() const;
    double max() const;

    double nonZeroWeight() const;
    double zeroWeight() const;
    double totalWeight() const;
    double maxWeight() const;

    double occupancy() const;
    double coverage() const;
    double maxLikelyhood() const;
    double span() const;

    double quantile(double q) const;
    std::pair<double,double> quantileRange(double min, double max);
    double probLessThan(double v) const;
    double probBetween(double l, double u) const;

    // Estimate of P(this < Y)
    // Uses rangeWeight on this vs impulses of Y
    double estProbLessThan(const CPHistogram& Y) const;

    // Apply a function of <range,weight> to impulses over the
    // specified range.  If min/max are not bin boundaries, use an
    // impulse representing the proportion of the bin that is within
    // the range.

    // a dead-simple function to apply
    static double product(double v, double w) { return(v*w); };
    // range specified directly by value
    double applyOnRange(double min = 0, 
                        double max = std::numeric_limits<double>::max(),
                        CPHistFunc F = &CPHistogram::product);
    // range specified indirectly by quantile points
    double applyOnQuantile(double min = 0, double max = 1, 
                           CPHistFunc F = &CPHistogram::product);

    void clear();
    void clearList();

    // if min and max are not given, range is determined by the data.
    // range will expand to fit the data in any case, but will not shrink
    void buildFromList(unsigned bincount, double totalWeight, 
                       double min = std::numeric_limits<double>::max(), 
                       double max = 0);
    void addToList(double v, double w = 1.0);
    void addToList(const WeightedValue& wv);

    // returns true on success, false on error
    bool serialize(unsigned ID, FILE* f) const;
    // returns ID on success, -1 on errro
    int deserialize(unsigned bincount, double totalweight, FILE* f);
    void print(llvm::raw_ostream& stream) const;
    void printStats(llvm::raw_ostream& stream) const;

    double overlap(const CPHistogram& other, bool includeZero) const;


	protected:

    class Stats {
    public:
      double sumOfSquares;
      double sumOfValues;
      double sumOfWeights;
      double totalWeight;

      Stats() {};
      Stats(const WeightedValueVec& vals);
      ~Stats() {};

      Stats& operator=(const Stats& s);
      void clear() {sumOfSquares=sumOfValues=sumOfWeights=totalWeight = 0;};
      void combineStats(const Stats& s2);  // merge s2 into self
      double mean(bool inclZeros=false) const;
      double stdev(bool inclZeros=false) const;
      void print(llvm::raw_ostream& stream);

      // Phi, the CDF using the mean and stdev of this Stats
      // ie, P(a < x) assuming a normal using our mean and stdev
      double phi(double x) const;
    };

    Stats _stats;
    double _min;
    double _max;
    unsigned _bincount;
    double* _bins;

    //void clearBins() {setBinCount(0)};
    void setBinCount(unsigned n);
    void copyBins(const CPHistogram& other);
		void setBinWeight(unsigned b, double w);
    void setRange(double min, double max);

    double addToBin(unsigned b, double w);
    //void add(Range r, double w);

    // update incremental statistics with weights/values from list
    //void updateStats(const WeightedValueVec vals);
    // reset stats by estimating from current histogram
    //void estimateStats();

	private:
    static int HistID;  //debug
    int _id;  //debug
		WeightedValueList _addList;
  };

}

#endif
