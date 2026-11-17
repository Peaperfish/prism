# PRISM_CHECK_LIBULTRAHDR (off by default): checks the Ultra HDR JPEGs Prism writes with
# libultrahdr, as decided in #104 and the 0.8.0 design (section 6). Test only: libultrahdr is
# linked into prism_uhdr_check alone, never into prism_core, prism or prism_gui, and it ships in
# nothing. Needs libultrahdr's pkg-config file (libuhdr), e.g. `brew install libultrahdr`.
#
# Two ctest entries: UltraHdrLibultrahdr.WriteFixtures runs prism_uhdr_write_fixtures (links
# prism_core, not libultrahdr) to write the files and what Prism's own reader shows, and
# UltraHdrLibultrahdr.Check runs prism_uhdr_check on them (links libultrahdr, not prism_core).

find_package(PkgConfig REQUIRED)
pkg_check_modules(PRISM_UHDR REQUIRED IMPORTED_TARGET libuhdr>=2.0.2)
message(STATUS "PRISM_CHECK_LIBULTRAHDR: libultrahdr ${PRISM_UHDR_VERSION}")

add_executable(prism_uhdr_write_fixtures write_fixtures.cpp)
target_link_libraries(prism_uhdr_write_fixtures PRIVATE prism_core prism_warnings)
target_include_directories(prism_uhdr_write_fixtures SYSTEM PRIVATE
                           $<TARGET_PROPERTY:stb,INTERFACE_INCLUDE_DIRECTORIES>)

add_executable(prism_uhdr_check check_with_libultrahdr.cpp)
target_link_libraries(prism_uhdr_check PRIVATE PkgConfig::PRISM_UHDR prism_warnings)
target_compile_features(prism_uhdr_check PRIVATE cxx_std_20)

set(prism_uhdr_fixtures ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
add_test(NAME UltraHdrLibultrahdr.WriteFixtures
         COMMAND prism_uhdr_write_fixtures ${prism_uhdr_fixtures})
set_tests_properties(UltraHdrLibultrahdr.WriteFixtures PROPERTIES FIXTURES_SETUP prism_uhdr_files)
add_test(NAME UltraHdrLibultrahdr.Check
         COMMAND prism_uhdr_check ${prism_uhdr_fixtures}
                 ${PROJECT_SOURCE_DIR}/third_party/icc/sRGB-v2-magic.icc chart-n2 chart-n3.5)
set_tests_properties(UltraHdrLibultrahdr.Check PROPERTIES FIXTURES_REQUIRED prism_uhdr_files)
