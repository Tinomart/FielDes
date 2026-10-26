# The FielDes application (Qt): editor, viewport, model tree, section viewer
if (NOT DEFINED FIELDES_BUILD_FROM_ROOT)
    message(FATAL_ERROR "CMake must be called against the top-level directory")
endif()

# Instruct CMake to run moc, uic, and rcc automatically when needed.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5OpenGL REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5Concurrent REQUIRED)

set(SRCS
    src/main.cpp
    src/app.cpp
    src/appwindow.cpp
    src/args.cpp
    src/arrow.cpp
    src/automation.cpp
    src/axes.cpp
    src/background.cpp
    src/bbox.cpp
    src/busy.cpp
    src/carddrag.cpp
    src/camera.cpp
    src/color.cpp
    src/documentation.cpp
    src/editor.cpp
    src/fileassoc.cpp
    src/findbar.cpp
    src/focuscue.cpp
    src/popout.cpp
    src/i18n.cpp
    src/icons.cpp
    src/language.cpp
    src/primitives_dialog.cpp
    src/live_panel.cpp
    src/scenetree.cpp
    src/script.cpp
    src/result_panel.cpp
    src/section.cpp
    src/settings.cpp
    src/shader.cpp
    src/shortcuts.cpp
    src/shape.cpp
    src/symmetry.cpp
    src/syntax.cpp
    src/theme.cpp
    src/tutorial.cpp
    src/typeicons.cpp
    src/view.cpp
    src/window.cpp
    src/workplane.cpp

    src/python/formatter.cpp
    src/python/interpreter.cpp
    src/python/language.cpp
    src/python/syntax.cpp

    resources.qrc
)

set(QOBJECTS
    include/fieldes/app.hpp
    include/fieldes/appwindow.hpp
    include/fieldes/automation.hpp
    include/fieldes/busy.hpp
    include/fieldes/camera.hpp
    include/fieldes/documentation.hpp
    include/fieldes/editor.hpp
    include/fieldes/findbar.hpp
    include/fieldes/interpreter.hpp
    include/fieldes/language.hpp
    include/fieldes/primitives_dialog.hpp
    include/fieldes/live_panel.hpp
    include/fieldes/scenetree.hpp
    include/fieldes/script.hpp
    include/fieldes/result_panel.hpp
    include/fieldes/section.hpp
    include/fieldes/carddrag.hpp
    include/fieldes/focuscue.hpp
    include/fieldes/shape.hpp
    include/fieldes/shortcuts.hpp
    include/fieldes/symmetry.hpp
    include/fieldes/syntax.hpp
    include/fieldes/tutorial.hpp
    include/fieldes/view.hpp
    include/fieldes/window.hpp
    include/fieldes/workplane.hpp
)

if(WIN32)
    add_executable(FielDes WIN32 ${SRCS} ${QOBJECTS} deploy/windows/icon.rc)
else()
    add_executable(FielDes ${SRCS} ${QOBJECTS})
endif()

# Where FielDes.exe is built: set FIELDES_OUTPUT_DIR to the folder the program is run from (dist/FielDes) and it is built straight into it -- the libraries are put beside it by the
# steps below -- so that there is no copy of the build to make.  Empty: in the build tree, as ever.  (A running FielDes.exe cannot be replaced: close it before the link step.)
set(FIELDES_OUTPUT_DIR "" CACHE PATH "Where FielDes.exe is built (empty: in the build tree)")
if(FIELDES_OUTPUT_DIR)
    set_target_properties(FielDes PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${FIELDES_OUTPUT_DIR}")
endif()

target_include_directories(FielDes PUBLIC include)
if(MSVC)
    # a map of where the functions are: a crash report names places as module + offset, read with it (kept in the build folder: with FIELDES_OUTPUT_DIR the
    # program is built into the folder it is run from, and the map is not a part of it)
    target_link_options(FielDes PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/FielDes.map")
endif()
target_include_directories(FielDes PRIVATE ${Python3_INCLUDE_DIRS})
target_link_libraries(FielDes
    libfive libfive-stdlib
    ${Python3_LIBRARIES}
    Qt5::Widgets
    Qt5::Gui
    Qt5::OpenGL
    Qt5::Concurrent
)
target_compile_definitions(FielDes PRIVATE FIELDES_WITH_PYTHON FIELDES_VERSION="${PROJECT_VERSION}")

if (MSVC)
    # Copy the DLLs into the build directory, so that FielDes.exe can find them
    add_custom_command(TARGET FielDes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:libfive>
            $<TARGET_FILE_DIR:FielDes>)
    add_custom_command(TARGET FielDes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:libfive-stdlib>
            $<TARGET_FILE_DIR:FielDes>)
    # The kernel's own dependency (manifold.dll): FielDes.exe doesn't link
    # it directly, so vcpkg's app-local copy step doesn't bring it
    find_package(manifold CONFIG REQUIRED)
    add_custom_command(TARGET FielDes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:manifold::manifold>
            $<TARGET_FILE_DIR:FielDes>)
endif(MSVC)
