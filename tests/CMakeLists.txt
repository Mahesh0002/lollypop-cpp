# Tests
find_package(GTest REQUIRED)

# Storage tests - DISABLED: missing implementations for get_track_count, get_album_count, get_artist_count, update_track
# add_executable(test_storage
#     test_storage.cpp
# )
# 
# target_link_libraries(test_storage PRIVATE
#     lollypop-storage
#     ${SQLITE3_LIBRARIES}
#     ${GLIB_LIBRARIES}
#     GTest::gtest
#     GTest::gtest_main
# )
# 
# add_test(NAME storage_tests COMMAND test_storage)

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

# EventBus tests
add_executable(test_eventbus
    test_eventbus.cpp
)

target_link_libraries(test_eventbus PRIVATE
    lollypop-backend
    lollypop-storage
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME eventbus_tests COMMAND test_eventbus)

# QueueManager tests
add_executable(test_queue_manager
    test_queue_manager.cpp
)

target_link_libraries(test_queue_manager PRIVATE
    lollypop-backend
    lollypop-storage
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME queue_manager_tests COMMAND test_queue_manager)

# Seek tests
add_executable(test_seek
    test_seek.cpp
)

target_link_libraries(test_seek PRIVATE
    lollypop-backend
    lollypop-storage
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME seek_tests COMMAND test_seek)

# Authority routing tests
add_executable(test_authority
    test_authority.cpp
)

target_link_libraries(test_authority PRIVATE
    lollypop-backend
    lollypop-storage
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME authority_tests COMMAND test_authority)

# Incremental scan tests
add_executable(test_scan_incremental
    test_scan_incremental.cpp
)

target_link_libraries(test_scan_incremental PRIVATE
    lollypop-backend
    lollypop-storage
    ${SQLITE3_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME scan_incremental_tests COMMAND test_scan_incremental)