//===- llvm/Transforms/IPO.h - Interprocedural Transformations --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the FDO transformations library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_FDO_H
#define LLVM_TRANSFORMS_FDO_H


namespace llvm {

  class ModulePass;
  //class Pass;
  //class Function;
  //class BasicBlock;
  //class GlobalValue;


  // FDO Inlining
  ModulePass* createFDOInlinerPass();

} // End llvm namespace

#endif
