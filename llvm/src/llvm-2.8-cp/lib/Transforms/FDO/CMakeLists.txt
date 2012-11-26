add_llvm_library(LLVMfdo
  CPCallRecord.cpp
  FDOInliner.cpp
  )

target_link_libraries (LLVMfdo)
