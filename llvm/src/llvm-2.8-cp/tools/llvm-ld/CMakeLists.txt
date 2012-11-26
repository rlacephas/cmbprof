set(LLVM_LINK_COMPONENTS ipo fdo scalaropts linker archive bitwriter)

add_llvm_tool(llvm-ld
  Optimize.cpp
  llvm-ld.cpp
  )
