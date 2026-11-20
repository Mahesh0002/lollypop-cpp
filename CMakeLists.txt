cmake_minimum_required(VERSION 3.20)
project(lollypop-cpp VERSION 0.1.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2")
endif()

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0 gobject-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)
# GStreamer for audio playback
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-audio-1.0 gstreamer-pbutils-1.0)
# CAVA is optional - for audio visualization
pkg_check_modules(CAVA QUIET cava)
if(CAVA_FOUND)
    message(STATUS "CAVA found: ${CAVA_LIBRARIES}")
    set(HAVE_CAVA 1)
else()
    message(STATUS "CAVA not found, audio visualization will use fallback")
    set(HAVE_CAVA 0)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/frontend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/storage/include
    ${GTK4_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
)

# Link libraries
set(LIBRARIES
    ${GTK4_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
)

# Add subdirectories
add_subdirectory(frontend)
add_subdirectory(backend)
add_subdirectory(storage)

# Main executable
add_executable(lollypop-cpp
    src/main.cpp
)

target_link_libraries(lollypop-cpp PRIVATE
    lollypop-frontend
    lollypop-backend
    lollypop-storage
    ${LIBRARIES}
)

# Copy resources to build directory
add_custom_command(TARGET lollypop-cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/style.css
        ${CMAKE_CURRENT_BINARY_DIR}/resources/style.css
    COMMENT "Copying resources to build directory"
)

# Install
install(TARGETS lollypop-cpp DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/lollypop-cpp)

# Enable testing
enable_testing()
add_subdirectory(tests)