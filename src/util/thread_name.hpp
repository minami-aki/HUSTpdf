#pragma once

#include <string>

namespace pdfread {

// Đặt tên cho luồng hiện tại: tên hiển thị trong log và (trên Windows) tên luồng
// mà debugger / profiler thấy.
void name_current_thread(std::string name);

}  // namespace pdfread
