cmake_minimum_required(VERSION 3.10)
project(calibre-companion)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# PocketBook SDK path (will be set by configure script)
set(TOOLCHAIN_PATH "${CMAKE_SOURCE_DIR}/SDK/SDK_6.3.0/SDK-B288" CACHE PATH "Path to PocketBook SDK")

# Set the toolchain for cross-compilation
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/toolchain.cmake")
endif()

# Include directories
include_directories(
    ${TOOLCHAIN_PATH}/usr/arm-obreey-linux-gnueabi/sysroot/usr/include
    ${TOOLCHAIN_PATH}/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2
)

# Link directories
link_directories(
    ${TOOLCHAIN_PATH}/usr/arm-obreey-linux-gnueabi/sysroot/usr/lib
)

# Source files
set(SOURCES
    src/main.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    inkview
    freetype
)

# Set output name
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "calibre-companion.app")

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION .
)