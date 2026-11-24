# Tools

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)

add_executable(fetch_lyrics fetch_lyrics.cpp)

target_include_directories(fetch_lyrics PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/storage/include
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(fetch_lyrics PRIVATE
    lollypop-storage
    lollypop-backend
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    PkgConfig::LIBCURL
    Threads::Threads
)