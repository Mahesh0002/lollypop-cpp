# Frontend - GTK4 Presentation Layer

file(GLOB_RECURSE FRONTEND_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/**/*.cpp
)

file(GLOB_RECURSE FRONTEND_HEADERS CONFIGURE_DEPENDS
    include/*.hpp
    include/**/*.hpp
)

add_library(lollypop-frontend STATIC ${FRONTEND_SOURCES} ${FRONTEND_HEADERS})

target_include_directories(lollypop-frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/storage/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/shaders
    /usr/include/libadwaita-1
    /usr/include/gtk-4.0
)

target_link_libraries(lollypop-frontend PUBLIC
    ${GTK4_LIBRARIES}
    ${LIBADWAITA_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    lollypop-backend
    lollypop-storage
)

target_compile_features(lollypop-frontend PUBLIC cxx_std_20)
target_compile_definitions(lollypop-frontend PRIVATE
    LOLLYPOP_DATA_DIR="${CMAKE_SOURCE_DIR}/resources")