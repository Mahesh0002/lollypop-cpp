# Tests
find_package(GTest REQUIRED)

# Storage tests
add_executable(test_storage
    test_storage.cpp
)

target_link_libraries(test_storage PRIVATE
    lollypop-storage
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME storage_tests COMMAND test_storage)

# Backend tests
add_executable(test_backend
    test_backend.cpp
)

target_link_libraries(test_backend PRIVATE
    lollypop-backend
    lollypop-storage
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME backend_tests COMMAND test_backend)