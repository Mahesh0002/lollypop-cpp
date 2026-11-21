# Backend - Core Logic, Audio Engine, Business Logic

file(GLOB_RECURSE BACKEND_SOURCES
    src/*.cpp
    src/**/*.cpp
)

file(GLOB_RECURSE BACKEND_HEADERS
    include/*.hpp
    include/**/*.hpp
)

add_library(lollypop-backend STATIC ${BACKEND_SOURCES} ${BACKEND_HEADERS})

target_include_directories(lollypop-backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/storage/include
    ${CMAKE_SOURCE_DIR}/include
)

# Find GStreamer and glibmm (for MPRIS D-Bus integration)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-audio-1.0
    gstreamer-app-1.0
    gstreamer-pbutils-1.0
)
pkg_check_modules(GIOMM REQUIRED IMPORTED_TARGET giomm-2.68)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
find_package(Threads REQUIRED)

target_link_libraries(lollypop-backend PUBLIC
    PkgConfig::GSTREAMER
    PkgConfig::GIOMM
    PkgConfig::TAGLIB
    Threads::Threads
    lollypop-storage
)

target_include_directories(lollypop-backend PUBLIC
    ${GSTREAMER_INCLUDE_DIRS}
    ${GIOMM_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
)

target_compile_features(lollypop-backend PUBLIC cxx_std_20)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lollypop-backend PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion
        -Wformat=2
    )
endif()
