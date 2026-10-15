# Builds the rgbmatrix Python extensions (core, graphics) through scikit-build-core.
cmake_minimum_required(VERSION 3.22)
project(rgbmatrix VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(Threads REQUIRED)

# The busy-wait loops in lib/gpio.cc are calibrated iteration counts, so the
# optimization level is part of the panel timing. Keep -O3, and don't build
# with -Os, which changes the loop shape.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The panel driver, compiled once and linked into both extensions.
add_library(rgbmatrix_driver STATIC
    lib/bdf-font.cc
    lib/framebuffer.cc
    lib/gpio.cc
    lib/graphics.cc
    lib/hardware-mapping.c
    lib/led-matrix.cc
    lib/options.cc
    lib/thread.cc
)
set_target_properties(rgbmatrix_driver PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(rgbmatrix_driver PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_include_directories(rgbmatrix_driver PUBLIC include)
target_link_libraries(rgbmatrix_driver PUBLIC Threads::Threads)

set(PYBINDING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/rgbmatrix)
foreach(module core graphics)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${module}.cpp
        COMMAND Python::Interpreter -m cython -3 --cplus ${PYBINDING_DIR}/${module}.pyx
                --output-file ${CMAKE_CURRENT_BINARY_DIR}/${module}.cpp
        DEPENDS ${PYBINDING_DIR}/${module}.pyx ${PYBINDING_DIR}/core.pxd ${PYBINDING_DIR}/graphics.pxd
                ${PYBINDING_DIR}/cppinc.pxd
        VERBATIM)
    python_add_library(${module} MODULE WITH_SOABI ${CMAKE_CURRENT_BINARY_DIR}/${module}.cpp)
    target_link_libraries(${module} PRIVATE rgbmatrix_driver)
    install(TARGETS ${module} DESTINATION rgbmatrix)
endforeach()
install(FILES ${PYBINDING_DIR}/__init__.py DESTINATION rgbmatrix)
