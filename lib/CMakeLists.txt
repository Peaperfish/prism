add_library(prism_core STATIC
  color.cpp
  histogram.cpp
  image.cpp
  parallel.cpp
  pipeline.cpp
  version.cpp)

# PUBLIC: anything linking prism_core also gets include/ on its include path, so it can
# write #include "prism/version.hpp". The "prism/" prefix avoids name clashes with other libraries.
target_include_directories(prism_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

# PUBLIC so users of prism_core are also compiled as C++20 (our headers may need it).
target_compile_features(prism_core PUBLIC cxx_std_20)

# The version comes from project(VERSION ...), so it is set in one place.
target_compile_definitions(prism_core PRIVATE PRISM_VERSION="${PROJECT_VERSION}")

# PRIVATE: stb is an implementation detail; callers of prism_core don't see stb headers.
target_link_libraries(prism_core PRIVATE stb prism_warnings)
