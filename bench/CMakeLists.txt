# A plain executable, deliberately not registered with ctest: it takes seconds and needs a
# Release build to mean anything. Run it by hand: ./build-release/bench/prism_bench
add_executable(prism_bench prism_bench.cpp)
# prism_gui_core brings prism_core with it (render_preview, for the GUI preview row).
target_link_libraries(prism_bench PRIVATE prism_gui_core prism_warnings)
prism_copy_libraw_dll(prism_bench)
