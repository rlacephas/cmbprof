/*===-- ProfileInfoTypes.h - Profiling info shared constants ------*- C -*-===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
|*===----------------------------------------------------------------------===*|
|*
|* This file defines constants shared by the various different profiling
|* runtime libraries and the LLVM C++ profile info loader. It must be a
|* C header because, at present, the profiling runtimes are written in C.
|*
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_ANALYSIS_PROFILEINFOTYPES_H
#define LLVM_ANALYSIS_PROFILEINFOTYPES_H

#define CP_HISTOGRAM_BIN_EPSILON 0.01

/* IDs to distinguish between those path counters stored in hashses vs arrays */
#define PP_ARRAY 0
#define PP_HASH  1

enum ProfilingType {
  ArgumentInfo     = 1, /* The command line argument block */
  FunctionInfo     = 2, /* Function profiling information  */
  BlockInfo        = 3, /* Block profiling information     */
  EdgeInfo         = 4, /* Edge profiling information      */
  PathInfo         = 5, /* Path profiling information      */
  BBTraceInfo      = 6, /* Basic block trace information   */
  OptEdgeInfo      = 7, /* Edge profiling information, optimal version */
  CombinedEdgeInfo = 8, /* Combined edge profiling information */
  CombinedPathInfo = 9  /* Combined path profiling information */
};

/*
 * The header for tables that map path numbers to path counters.
 */
typedef struct {
  unsigned fnNumber; /* function number for these counters */
  unsigned numEntries;   /* number of entries stored */
} PathHeader;

/*
 * Describes an entry in a tagged table for path counters.
 */
typedef struct {
  unsigned pathNumber;
  unsigned pathCounter;
} PathTableEntry;

/*
 * Defines a bin in a combined profiling histogram
 */
typedef struct {
	unsigned char index;
	double weight;
} CombinedProfHistogramBin;

/*
 * Combined profiling histogram information
 */
typedef struct {
	double weight;
	double sumOfSquares;
	double binWidth;
	double binMin;
	double binMax;
	unsigned edgeId;
	unsigned nonZeros;
	unsigned char binsUsed;
} CombinedProfEntry;

#endif /* LLVM_ANALYSIS_PROFILEINFOTYPES_H */
