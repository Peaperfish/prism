# A plain executable, deliberately not registered with ctest: it takes seconds and needs a
# Release build to mean anything. Run it by hand: ./build-release/bench/prism_bench
add_executable(prism_bench prism_bench.cpp)
target_link_libraries(prism_bench PRIVATE prism_core prism_warnings)
prism_copy_libraw_dll(prism_bench)
