#pragma once
// Header MuPDF dùng nội bộ trong src/pdf/*.cpp. Các header công khai của module
// pdf chỉ forward-declare kiểu MuPDF để phần còn lại của app không phải include fitz.h.
//
// Quy tắc khi viết fz_try:
//  - Trong khối fz_try chỉ gọi hàm C của MuPDF, không tạo đối tượng C++ có destructor,
//    không ném exception C++ (fz_throw dùng longjmp).
//  - Biến gán trong fz_try mà fz_always / fz_catch dùng tới phải khai báo fz_var().
//  - Chuyển lỗi thành pdf::Error SAU khối fz_catch.

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

#ifdef _MSC_VER
// C4611: setjmp (trong fz_try) cùng hàm với đối tượng C++ có destructor. Trên MSVC x64,
// longjmp unwind stack như exception nên vẫn an toàn; ngoài ra các khối fz_try ở đây
// không tạo đối tượng C++ nào giữa setjmp và longjmp (xem quy tắc ở trên).
#pragma warning(disable : 4611)
#endif
