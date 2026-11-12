find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(pdfread_tests
    util/test_concurrent_queue.cpp
    util/test_latest_slot.cpp
    util/test_thread_pool.cpp
    util/test_log_timer.cpp
)
target_link_libraries(pdfread_tests PRIVATE pdfread_util pdfread_options Catch2::Catch2WithMain)

catch_discover_tests(pdfread_tests)
