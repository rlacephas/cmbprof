set(LLVM_LINK_COMPONENTS asmparser ipo fdo bitreader bitwriter)

add_llvm_tool(llvm-extract
  llvm-extract.cpp
  )
