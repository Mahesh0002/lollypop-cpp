# Storage - SQLite Persistence Layer

file(GLOB_RECURSE STORAGE_SOURCES
    src/*.cpp
    src/**/*.cpp
)

file(GLOB_RECURSE STORAGE_HEADERS
    include/*.hpp
    include/**/*.hpp
)

add_library(lollypop-storage STATIC ${STORAGE_SOURCES} ${STORAGE_HEADERS})

target_include_directories(lollypop-storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lollypop-storage PUBLIC
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
)

target_compile_features(lollypop-storage PUBLIC cxx_std_20)