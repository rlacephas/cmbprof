//===-- ARMMCAsmInfo.cpp - ARM asm properties -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the ARMMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "ARMMCAsmInfo.h"
using namespace llvm;

static const char *const arm_asm_table[] = {
  "{r0}", "r0",
  "{r1}", "r1",
  "{r2}", "r2",
  "{r3}", "r3",
  "{r4}", "r4",
  "{r5}", "r5",
  "{r6}", "r6",
  "{r7}", "r7",
  "{r8}", "r8",
  "{r9}", "r9",
  "{r10}", "r10",
  "{r11}", "r11",
  "{r12}", "r12",
  "{r13}", "r13",
  "{r14}", "r14",
  "{lr}", "lr",
  "{sp}", "sp",
  "{ip}", "ip",
  "{fp}", "fp",
  "{sl}", "sl",
  "{memory}", "memory",
  "{cc}", "cc",
  0,0
};

ARMMCAsmInfoDarwin::ARMMCAsmInfoDarwin() {
  AsmTransCBE = arm_asm_table;
  Data64bitsDirective = 0;
  CommentString = "@";
  SupportsDebugInformation = true;

  // Exceptions handling
  ExceptionsType = ExceptionHandling::SjLj;
}

ARMELFMCAsmInfo::ARMELFMCAsmInfo() {
  // ".comm align is in bytes but .align is pow-2."
  AlignmentIsInBytes = false;

  Data64bitsDirective = 0;
  CommentString = "@";

  HasLEB128 = true;
  PrivateGlobalPrefix = ".L";
  WeakRefDirective = "\t.weak\t";
  HasLCOMMDirective = true;

  DwarfRequiresFrameSection = false;

  SupportsDebugInformation = true;
}
