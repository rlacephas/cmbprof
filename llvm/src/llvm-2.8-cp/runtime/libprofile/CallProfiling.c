/*===-- CGProfiling.c - Support library for callgraph profiling -----------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source      
|* License. See LICENSE.TXT for details.                                      
|* 
|*===----------------------------------------------------------------------===*|
|* 
|* This file implements the call back routines for the callgraph profiling
|* instrumentation pass.  This should be used with the -insert-call-profiling
|* LLVM pass.
|*
\*===----------------------------------------------------------------------===*/

#include "Profiling.h"
#include <stdlib.h>

static unsigned *ArrayStart;
static unsigned NumElements;

/* CallProfAtExitHandler - When the program exits, just write out the profiling
 * data.
 */
static void CallProfAtExitHandler() {
  write_profiling_data(CallInfo, ArrayStart, NumElements);
}


/* llvm_start_call_profiling - This is the main entry point of the callgraph
 * profiling library.  It is responsible for setting up the atexit handler.
 */
int llvm_start_call_profiling(int argc, const char **argv,
                              unsigned *arrayStart, unsigned numElements) {
  int Ret = save_arguments(argc, argv);
  ArrayStart = arrayStart;
  NumElements = numElements;
  atexit(CallProfAtExitHandler);
  return Ret;
}
