//===-- FDO.cpp ----------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the C bindings for libLLVMFDO.a
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Transforms/FDO.h"
#include "llvm/PassManager.h"
#include "llvm/Transforms/FDO.h"

using namespace llvm;

void LLVMAddFDOInlinerPass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createFDOInlinerPass());
}
