#pragma once
// Tiện ích dùng chung cho test cần MuPDF.

#include <filesystem>
#include <string>
#include <system_error>

#include "pdf/context.hpp"

namespace pdfread::testing {

inline pdf::RootContext& mupdf_root() {
    static pdf::RootContext root;
    return root;
}

// Gắn context MuPDF cho luồng test trong suốt một TEST_CASE.
struct MuPdfScope {
    MuPdfScope() { pdf::attach_thread(mupdf_root()); }
    ~MuPdfScope() { pdf::detach_thread(); }
    MuPdfScope(const MuPdfScope&) = delete;
    MuPdfScope& operator=(const MuPdfScope&) = delete;
};

// File tạm, tự xóa khi ra khỏi scope.
struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& name) : path(std::filesystem::temp_directory_path() / name) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

}  // namespace pdfread::testing
