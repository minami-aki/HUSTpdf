# Cờ biên dịch dùng chung cho mã của dự án (không áp cho thư viện bên thứ ba).
add_library(pdfread_options INTERFACE)

if(MSVC)
    target_compile_options(pdfread_options INTERFACE
        /W4
        /permissive-
        /utf-8              # mã nguồn và chuỗi literal là UTF-8 (tiếng Việt)
        /Zc:__cplusplus
        /Zc:preprocessor
    )
    target_compile_definitions(pdfread_options INTERFACE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
else()
    target_compile_options(pdfread_options INTERFACE
        -Wall -Wextra -Wpedantic
    )
endif()
