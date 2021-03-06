/* include/llvm/Config/llvm-config.h.  Generated from llvm-config.h.in by configure.  */
/*===-- llvm/config/llvm-config.h - llvm configure variable -------*- C -*-===*/
/*                                                                            */
/*                     The LLVM Compiler Infrastructure                       */
/*                                                                            */
/* This file is distributed under the University of Illinois Open Source      */
/* License. See LICENSE.TXT for details.                                      */
/*                                                                            */
/*===----------------------------------------------------------------------===*/

/* This file enumerates all of the llvm variables from configure so that
   they can be in exported headers and won't override package specific
   directives.  This is a C file so we can include it in the llvm-c headers.  */

/* To avoid multiple inclusions of these variables when we include the exported
   headers and config.h, conditionally include these.  */
/* TODO: This is a bit of a hack.  */
#ifndef CONFIG_H

/* Installation directory for binary executables */
#define LLVM_BINDIR "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp/bin"

/* Time at which LLVM was configured */
#define LLVM_CONFIGTIME "Mon Jun 13 17:31:35 MDT 2011"

/* Installation directory for data files */
#define LLVM_DATADIR "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp/share/llvm"

/* Installation directory for documentation */
#define LLVM_DOCSDIR "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp/share/doc/llvm"

/* Installation directory for config files */
#define LLVM_ETCDIR "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp/etc/llvm"

/* Host triple we were built on */
#define LLVM_HOSTTRIPLE "i686-pc-linux-gnu"

/* Installation directory for include files */
#define LLVM_INCLUDEDIR "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp/include"

/* Installation directory for .info files */
#define LLVM_INFODIR "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp/info"

/* Installation directory for libraries */
#define LLVM_LIBDIR "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp/lib"

/* Installation directory for man pages */
#define LLVM_MANDIR "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp/man"

/* Build multithreading support into LLVM */
#define LLVM_MULTITHREADED 1

/* LLVM architecture name for the native architecture, if available */
#define LLVM_NATIVE_ARCH X86

/* LLVM name for the native Target init function, if available */
#define LLVM_NATIVE_TARGET LLVMInitializeX86Target

/* LLVM name for the native TargetInfo init function, if available */
#define LLVM_NATIVE_TARGETINFO LLVMInitializeX86TargetInfo

/* LLVM name for the native AsmPrinter init function, if available */
#define LLVM_NATIVE_ASMPRINTER LLVMInitializeX86AsmPrinter

/* Define if this is Unixish platform */
#define LLVM_ON_UNIX 1

/* Define if this is Win32ish platform */
/* #undef LLVM_ON_WIN32 */

/* Define to path to circo program if found or 'echo circo' otherwise */
#define LLVM_PATH_CIRCO "/usr/bin/circo"

/* Define to path to dot program if found or 'echo dot' otherwise */
#define LLVM_PATH_DOT "/usr/bin/dot"

/* Define to path to dotty program if found or 'echo dotty' otherwise */
#define LLVM_PATH_DOTTY "/usr/bin/dotty"

/* Define to path to fdp program if found or 'echo fdp' otherwise */
#define LLVM_PATH_FDP "/usr/bin/fdp"

/* Define to path to Graphviz program if found or 'echo Graphviz' otherwise */
/* #undef LLVM_PATH_GRAPHVIZ */

/* Define to path to gv program if found or 'echo gv' otherwise */
#define LLVM_PATH_GV "/usr/bin/gv"

/* Define to path to neato program if found or 'echo neato' otherwise */
#define LLVM_PATH_NEATO "/usr/bin/neato"

/* Define to path to twopi program if found or 'echo twopi' otherwise */
#define LLVM_PATH_TWOPI "/usr/bin/twopi"

/* Installation prefix directory */
#define LLVM_PREFIX "/home/paul/research/Experiments/Cmbprof/llvm/llvm-2.8-cp"

#endif
