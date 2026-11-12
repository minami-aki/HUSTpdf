# llama.cpp (submodule third_party/llama.cpp, cố định tag v0.9.0).
#
# Chỉ backend CPU. GGML_BACKEND_DL + GGML_CPU_ALL_VARIANTS: build mọi biến thể CPU
# (SSE4.2 / AVX2 / AVX-512…) thành DLL riêng, lúc chạy ggml tự chọn biến thể tốt nhất
# cho máy. Chế độ này bắt buộc llama.cpp build dạng thư viện động (DLL đặt cạnh exe).
# OpenMP tắt: OpenMP của MSVC cần vcomp140.dll (VC Redist); ggml dùng threadpool riêng.

set(_pdfread_saved_shared_libs ${BUILD_SHARED_LIBS})
set(_pdfread_saved_cxx_standard ${CMAKE_CXX_STANDARD})
set(_pdfread_saved_msvc_runtime ${CMAKE_MSVC_RUNTIME_LIBRARY})
set(BUILD_SHARED_LIBS ON)
# Các DLL ggml/llama dùng CRT động (/MD): với CRT tĩnh mỗi DLL có heap riêng, mà registry
# backend của ggml truyền đối tượng C++ qua lại giữa các DLL -> hỏng heap.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# llama.cpp viết cho C++17: ở C++20 các chuỗi u8"..." thành char8_t và không biên dịch được.
set(CMAKE_CXX_STANDARD 17)

set(GGML_NATIVE           OFF CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL       ON  CACHE BOOL "" FORCE)
set(GGML_CPU_ALL_VARIANTS ON  CACHE BOOL "" FORCE)
set(GGML_OPENMP           OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL         OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP       OFF CACHE BOOL "" FORCE)

add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/llama.cpp ${CMAKE_BINARY_DIR}/third_party/llama.cpp)

set(BUILD_SHARED_LIBS ${_pdfread_saved_shared_libs})
set(CMAKE_CXX_STANDARD ${_pdfread_saved_cxx_standard})
set(CMAKE_MSVC_RUNTIME_LIBRARY ${_pdfread_saved_msvc_runtime})
