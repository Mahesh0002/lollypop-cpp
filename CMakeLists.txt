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

# AddressSanitizer option
option(ASAN "Enable AddressSanitizer" OFF)
if(ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fsanitize=address")
    message(STATUS "AddressSanitizer enabled")
endif()

# LeakSanitizer option (works with ASan)
option(LSAN "Enable LeakSanitizer" OFF)
if(LSAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=leak")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=leak")
    message(STATUS "LeakSanitizer enabled")
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2")
endif()

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(LIBADWAITA REQUIRED libadwaita-1)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0 gobject-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)

# Gettext for internationalization
find_package(Gettext REQUIRED)
set(GETTEXT_PACKAGE "lollypop-cpp")
add_definitions(-DGETTEXT_PACKAGE="${GETTEXT_PACKAGE}")
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

# GSK GLSL Compiler for shaders (GTK 4.14+)
find_program(GSK_GLSL_COMPILER gsk-glsl-compiler)
if(NOT GSK_GLSL_COMPILER)
    message(WARNING "gsk-glsl-compiler not found - shaders will not be compiled")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/frontend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/storage/include
    ${GTK4_INCLUDE_DIRS}
    ${LIBADWAITA_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
)

# Link libraries
set(LIBRARIES
    ${GTK4_LIBRARIES}
    ${LIBADWAITA_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
)

# Shader compilation
function(compile_gsk_shader OUTPUT_NAME VERT FRAG)
    if(GSK_GLSL_COMPILER)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME}.gskshader
            COMMAND ${GSK_GLSL_COMPILER}
                --vertex ${VERT}
                --fragment ${FRAG}
                --output ${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME}.gskshader
            DEPENDS ${VERT} ${FRAG}
            COMMENT "Compiling GSK shader: ${OUTPUT_NAME}"
        )
        set(${OUTPUT_NAME}_SHADER ${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME}.gskshader PARENT_SCOPE)
    else()
        message(WARNING "Skipping shader ${OUTPUT_NAME} - gsk-glsl-compiler not found")
    endif()
endfunction()

if(GSK_GLSL_COMPILER)
    # Compile all shaders
    compile_gsk_shader(glass_shader      src/ui/shaders/glass.vert      src/ui/shaders/glass.frag)
    compile_gsk_shader(glass_reflection_shader  src/ui/shaders/glass_reflection.vert  src/ui/shaders/glass_reflection.frag)
    compile_gsk_shader(glass_dial_shader      src/ui/shaders/glass_dial.vert      src/ui/shaders/glass_dial.frag)
    compile_gsk_shader(waveform_shader        src/ui/shaders/waveform.vert        src/ui/shaders/waveform.frag)
    compile_gsk_shader(morph_shader           src/ui/shaders/morph.vert           src/ui/shaders/morph.frag)

    # Shader target for building all shaders
    add_custom_target(shaders ALL
        DEPENDS
            ${glass_shader_SHADER}
            ${glass_reflection_shader_SHADER}
            ${glass_dial_shader_SHADER}
            ${waveform_shader_SHADER}
            ${morph_shader_SHADER}
        COMMENT "Compiling all GSK shaders"
    )

    # Install shaders
    install(FILES
        ${glass_shader_SHADER}
        ${glass_reflection_shader_SHADER}
        ${glass_dial_shader_SHADER}
        ${waveform_shader_SHADER}
        ${morph_shader_SHADER}
        DESTINATION share/lollypop-cpp/shaders
    )
endif()

# GResource compilation
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources)
if(GLIB_COMPILE_RESOURCES)
    # Generate a C source file that embeds resource data and registers it
    # with GLib at startup. Without this, g_resources_lookup_data() returns
    # NULL and gtk_css_provider_load_from_resource() fails.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lollypop_resources.c
        COMMAND ${GLIB_COMPILE_RESOURCES}
            --sourcedir ${CMAKE_CURRENT_SOURCE_DIR}/resources
            --generate-source
            --c-name=lollypop_resources
            --target ${CMAKE_CURRENT_BINARY_DIR}/lollypop_resources.c
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/lollypop.gresource.xml
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/lollypop.gresource.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/style.css
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/apple_ui.css
        COMMENT "Generating GResource C registration source"
    )
    add_custom_target(resources ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lollypop_resources.c)
else()
    message(WARNING "glib-compile-resources not found - resources will not be compiled")
endif()

# Add subdirectories
add_subdirectory(frontend)
add_subdirectory(backend)
add_subdirectory(storage)
add_subdirectory(tools)

# Main executable
if(GLIB_COMPILE_RESOURCES)
    set(LollypopSources
        src/main.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/lollypop_resources.c
    )
else()
    set(LollypopSources
        src/main.cpp
    )
endif()
if(GLIB_COMPILE_RESOURCES)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/lollypop_resources.c
        PROPERTIES LANGUAGE CXX)
endif()
add_executable(lollypop-cpp ${LollypopSources})

# Depend on resources and shaders (if available)
if(GLIB_COMPILE_RESOURCES)
    add_dependencies(lollypop-cpp resources)
endif()
if(GSK_GLSL_COMPILER)
    add_dependencies(lollypop-cpp shaders)
endif()

target_link_libraries(lollypop-cpp PRIVATE
    lollypop-frontend
    lollypop-backend
    lollypop-storage
    ${LIBRARIES}
)

# Copy resources to build directory
add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/style.css
        ${CMAKE_CURRENT_BINARY_DIR}/resources/style.css
    COMMENT "Copying resources to build directory"
)
add_dependencies(lollypop-cpp copy_resources)

# Install
install(TARGETS lollypop-cpp DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/lollypop-cpp)

# Enable testing
enable_testing()
add_subdirectory(tests)