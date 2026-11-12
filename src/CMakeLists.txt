find_package(Threads REQUIRED)

# --- util -------------------------------------------------------------------
add_library(pdfread_util STATIC
    util/log.cpp
    util/thread_name.cpp
    util/thread_pool.cpp
)
target_include_directories(pdfread_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pdfread_util
    PUBLIC Threads::Threads
    PRIVATE pdfread_options
)

# --- app --------------------------------------------------------------------
add_executable(pdfread app/main.cpp)
target_compile_definitions(pdfread PRIVATE PDFREAD_VERSION="${PROJECT_VERSION}")
target_link_libraries(pdfread PRIVATE pdfread_util pdfread_options)
