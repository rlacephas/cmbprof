/*===-- FDO.h - Feedback-Directed Optimization C Interface ------*- C++ -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMFDO.a                       *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TRANSFORMS_FDO_H
#define LLVM_C_TRANSFORMS_FDO_H

#include "llvm-c/Core.h"

#ifdef __cplusplus
extern "C" {
#endif

void LLVMAddFDOInlinerPass(LLVMPassManagerRef PM);

#ifdef __cplusplus
}
#endif /* defined(__cplusplus) */

#endif
