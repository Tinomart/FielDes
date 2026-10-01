# The kernel: field evaluation, meshing, STEP import, analysis (shared library "fieldes"),
# and its C++ standard library of shapes ("fieldes-stdlib")
if (NOT DEFINED FIELDES_BUILD_FROM_ROOT)
    message(FATAL_ERROR "CMake must be called against the top-level directory")
endif()

add_subdirectory(src)
add_subdirectory(stdlib)
