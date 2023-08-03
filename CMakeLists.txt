set(LLVM_LINK_COMPONENTS support)

add_clang_executable(loop-convert
  omploop_convert.cpp
  )
target_link_libraries(loop-convert
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )
