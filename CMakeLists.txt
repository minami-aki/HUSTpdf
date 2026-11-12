cmake_minimum_required(VERSION 3.25)

project(pdfread
    VERSION 0.1.0
    DESCRIPTION "Offline technical PDF reader with instant EN->VI translation"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PDFREAD_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)

# Mọi file chạy và DLL vào chung một thư mục: ggml nạp các DLL backend CPU từ thư mục
# của file chạy, và test cần các DLL đó cạnh pdfread_tests.exe.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(Llama)

add_subdirectory(src)

if(PDFREAD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
