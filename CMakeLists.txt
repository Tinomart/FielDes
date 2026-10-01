cmake_minimum_required(VERSION 3.12)
project(FielDes VERSION 0.1.0 LANGUAGES C CXX)

option(BUILD_APP "Build the FielDes application" ON)
option(ENABLE_DEBUG "Add debugging symbols to the binaries" OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

################################################################################
# Compiler settings

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC -pedantic -Werror=switch")
    if(ENABLE_DEBUG)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "-O0")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DEIGEN_NO_DEBUG")
else()
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    # /MP: compile a project's files in parallel
    set(CMAKE_CXX_FLAGS "/EHsc /WX /MP /D_USE_MATH_DEFINES /D_SCL_SECURE_NO_WARNINGS")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4267 /wd4244 /wd4305")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /DEIGEN_NO_DEBUG /arch:AVX2")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

################################################################################
# Dependencies (on Windows: vcpkg, see scripts/build-windows.ps1)

if(NOT MSVC)
    find_package(Boost REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(EIGEN REQUIRED eigen3>=3.2.92)
else()
    find_package(eigen3 REQUIRED)
    find_package(boost REQUIRED 1.65.0)
endif()

find_package(PNG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

if(BUILD_APP)
    find_package(Qt5Core 5.12 REQUIRED)
endif()

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -latomic")
endif()

message("FielDes ${PROJECT_VERSION}")
message("  kernel:       yes")
message("  application:  ${BUILD_APP}")

################################################################################

set(FIELDES_BUILD_FROM_ROOT true)

add_subdirectory(kernel)

if(BUILD_APP)
    add_subdirectory(app)
endif()
