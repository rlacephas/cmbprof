//===- CPFactory.h --------------------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Takes raw and/or combined profiles from one or more profile file
// and combines the like-typed profiles (edge/path/call) into a single
// combined profile of that type.
//
//===----------------------------------------------------------------------===//


#ifndef CPFACTORY_H
#define CPFACTORY_H


#include "llvm/Support/CommandLine.h"
#include "llvm/Analysis/ProfileInfoTypes.h"
#include "llvm/Analysis/CombinedProfile.h"

#include <vector>

namespace llvm {

  class Module;

  typedef std::vector<std::string> FilenameVec;

  class CPFactory {
  public:
    CPFactory(Module& M);
    ~CPFactory();

    bool buildProfiles(const FilenameVec& filenames);
    bool buildProfiles(cl::list<std::string>& filenames);
    bool buildProfiles(const std::string& filename);

    bool hasCallCP() {return(_callCP != NULL);};
    bool hasEdgeCP() {return(_edgeCP != NULL);};
    bool hasPathCP() {return(_pathCP != NULL);};

    // the caller of a 'take' method also takes responsibility for
    // deallocating the CP.  A CP can only be taken once.
    CombinedCallProfile* takeCallCP()
    { CombinedCallProfile* tmp = _callCP; _callCP = NULL; return(tmp); };
    
    CombinedEdgeProfile* takeEdgeCP()
    { CombinedEdgeProfile* tmp = _edgeCP; _edgeCP = NULL; return(tmp); };

    CombinedPathProfile* takePathCP()
    { CombinedPathProfile* tmp = _pathCP; _pathCP = NULL; return(tmp); };

    static const std::string& profilingTypeToString(ProfilingType p);

    void clear();

    // free the static data of CP classes (CPFactory itself doesn't have any)
    static void freeStaticData();

  protected:
    CombinedCallProfile* _callCP;
    CombinedEdgeProfile* _edgeCP;
    CombinedPathProfile* _pathCP;

    bool skipArgumentInfo(FILE* file);
    Module& _M;

  private:
    CPFactory(); // do not implement

  }; // CPFactory
  

} // namespace llvm

#endif // CPFACTORY_H
