# Triplet riêng của dự án: giống x64-windows-static (thư viện tĩnh, CRT tĩnh /MT),
# chỉ thêm cờ biên dịch cho từng port cụ thể.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

if(PORT STREQUAL "libmupdf")
    # Bỏ font dự phòng CJK (Source Han, Droid Sans Fallback, ~28 MB) khỏi file chạy.
    # PDF tiếng Trung/Nhật/Hàn KHÔNG nhúng font sẽ hiện ô vuông; tài liệu kỹ thuật
    # tiếng Anh không bị ảnh hưởng. Xem docs/DECISIONS.md.
    set(VCPKG_C_FLAGS "/DTOFU_CJK")
    set(VCPKG_CXX_FLAGS "/DTOFU_CJK")
endif()
