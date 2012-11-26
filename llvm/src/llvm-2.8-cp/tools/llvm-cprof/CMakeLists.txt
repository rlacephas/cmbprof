set(LLVM_LINK_COMPONENTS bitreader analysis)

add_llvm_tool(llvm-cprof
  llvm-cprof.cpp
  )
