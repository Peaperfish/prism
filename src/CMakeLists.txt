add_executable(prism main.cpp)
target_link_libraries(prism PRIVATE prism_core prism_warnings)

# Converts a camera profile (LightCraft JSON) to a matrix file for --raw-camera-matrix.
add_executable(prism_profile prism_profile.cpp)
target_link_libraries(prism_profile PRIVATE prism_core prism_warnings)
