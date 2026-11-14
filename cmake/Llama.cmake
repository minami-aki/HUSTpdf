# llama.cpp (submodule third_party/llama.cpp, cố định tag v0.9.0).
#
# GGML_BACKEND_DL: mọi backend là DLL riêng đặt cạnh exe, app chỉ nạp DLL cần dùng
# (translate/llama_translator.cpp). GGML_CPU_ALL_VARIANTS: build mọi biến thể CPU
# (SSE4.2 / AVX2 / AVX-512…), lúc chạy chọn biến thể tốt nhất cho máy.
# Backend GPU (M8): Vulkan (ggml-vulkan.dll, cần Vulkan SDK lúc build; lúc chạy chỉ cần driver)
# và CUDA (ggml-cuda.dll, cần CUDA Toolkit; lúc chạy cần thêm cudart/cuBLAS — gói bổ sung riêng).
# Mặc định bật khi tìm thấy bộ công cụ và không phải bản Debug (Debug không cần GPU, build nhanh).
# OpenMP tắt: OpenMP của MSVC cần vcomp140.dll (VC Redist); ggml dùng threadpool riêng.

set(_pdfread_gpu_default OFF)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_pdfread_gpu_default ON)
endif()

find_package(Vulkan QUIET COMPONENTS glslc)
set(_pdfread_vulkan_default OFF)
if(_pdfread_gpu_default AND Vulkan_FOUND AND Vulkan_glslc_FOUND)
    set(_pdfread_vulkan_default ON)
endif()
option(PDFREAD_VULKAN "Build the ggml Vulkan backend (needs the Vulkan SDK)" ${_pdfread_vulkan_default})

include(CheckLanguage)
check_language(CUDA)
set(_pdfread_cuda_default OFF)
if(_pdfread_gpu_default AND CMAKE_CUDA_COMPILER)
    set(_pdfread_cuda_default ON)
endif()
option(PDFREAD_CUDA "Build the ggml CUDA backend (needs the CUDA Toolkit)" ${_pdfread_cuda_default})
# Kiến trúc GPU NVIDIA được biên dịch sẵn. "native" = GPU của máy build (nhanh, để phát triển);
# bản phát hành truyền danh sách đầy đủ (tools/package.ps1).
set(PDFREAD_CUDA_ARCHS "native" CACHE STRING "CMAKE_CUDA_ARCHITECTURES for the ggml CUDA backend")
message(STATUS "pdfread: Vulkan backend ${PDFREAD_VULKAN}, CUDA backend ${PDFREAD_CUDA}")

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
set(GGML_VULKAN           ${PDFREAD_VULKAN} CACHE BOOL "" FORCE)
set(GGML_CUDA             ${PDFREAD_CUDA} CACHE BOOL "" FORCE)
if(PDFREAD_CUDA)
    set(CMAKE_CUDA_ARCHITECTURES ${PDFREAD_CUDA_ARCHS})
endif()
set(LLAMA_OPENSSL         OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP       OFF CACHE BOOL "" FORCE)

add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/llama.cpp ${CMAKE_BINARY_DIR}/third_party/llama.cpp)

# CUDA: chép thư viện runtime NVIDIA mà ggml-cuda.dll cần vào thư mục bin của bản build để chạy
# và test tại chỗ (bản phát hành đặt chúng vào gói bổ sung CUDA riêng, xem tools/package.ps1).
if(PDFREAD_CUDA)
    find_package(CUDAToolkit REQUIRED)
    file(GLOB _pdfread_cuda_runtime
        "${CUDAToolkit_BIN_DIR}/cudart64_*.dll" "${CUDAToolkit_BIN_DIR}/x64/cudart64_*.dll"
        "${CUDAToolkit_BIN_DIR}/cublas64_*.dll" "${CUDAToolkit_BIN_DIR}/x64/cublas64_*.dll"
        "${CUDAToolkit_BIN_DIR}/cublasLt64_*.dll" "${CUDAToolkit_BIN_DIR}/x64/cublasLt64_*.dll")
    if(_pdfread_cuda_runtime)
        file(COPY ${_pdfread_cuda_runtime} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    else()
        message(WARNING "pdfread: CUDA runtime DLLs not found in ${CUDAToolkit_BIN_DIR}")
    endif()
endif()

set(BUILD_SHARED_LIBS ${_pdfread_saved_shared_libs})
set(CMAKE_CXX_STANDARD ${_pdfread_saved_cxx_standard})
set(CMAKE_MSVC_RUNTIME_LIBRARY ${_pdfread_saved_msvc_runtime})
