# stb is header-only: the implementation is compiled exactly once, in stb_impl.cpp.
add_library(stb STATIC stb_impl.cpp)

# SYSTEM marks the headers as third-party so their warnings don't show up in our builds.
target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(stb PUBLIC cxx_std_20)

# Vendored code we don't edit: hide its deprecation warnings (e.g. sprintf on macOS) for this target only.
if(NOT MSVC)
  target_compile_options(stb PRIVATE -Wno-deprecated-declarations)
endif()
