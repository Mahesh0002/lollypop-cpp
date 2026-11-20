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

target_link_libraries(lollypop-backend PUBLIC
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    lollypop-storage
)

target_compile_features(lollypop-backend PUBLIC cxx_std_20)