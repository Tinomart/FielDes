add_library(libfive-stdlib SHARED stdlib.cpp stdlib_impl.cpp)

# The library is named 'fieldes-stdlib' on all platforms
set_target_properties(libfive-stdlib PROPERTIES PREFIX "" OUTPUT_NAME fieldes-stdlib)
target_link_libraries(libfive-stdlib libfive)
