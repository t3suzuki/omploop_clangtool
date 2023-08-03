set(LLVM_LINK_COMPONENTS support)

add_clang_executable(omploop-convert
  omploop_convert.cpp
  )
target_link_libraries(omploop-convert
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )
