# Các quyết định thiết kế đã chốt

Bổ sung cho [CLAUDE.md](../CLAUDE.md). Mỗi mục ghi ngày chốt; muốn đổi thì sửa ở đây trước rồi mới sửa code.

## 2026-11-12 — chốt trước M0

| # | Vấn đề | Quyết định | Lý do chính |
|---|---|---|---|
| 1 | Định dạng cấu hình lĩnh vực | **JSON** (`data/domains.json`), đọc bằng **nlohmann/json** (thư viện ngoài danh sách mục 2, đã được đồng ý) | Header-only, có trên vcpkg, dùng lại được cho cài đặt và báo cáo eval. YAML cần thư viện nặng hơn hoặc API khó dùng. |
| 2 | Framework unit test | **Catch2 v3** | `GENERATE` / bảng ca thử hợp với hàng trăm ca tách câu; tích hợp CTest qua `catch_discover_tests`. |
| 3a | Đưa MuPDF vào | **vcpkg port `libmupdf`** (1.28.6), cố định bằng `builtin-baseline`, **không** bật feature `ocr` | MuPDF không có CMake chính thức; port vcpkg đúng nhánh 1.28.x. |
| 3b | Đưa llama.cpp vào | **git submodule** `third_party/llama.cpp`, cố định tag **v0.9.0** (2026-11-04, tương ứng nightly b12110). Chỉ backend CPU, tắt server/tools; build với `GGML_NATIVE=OFF`, `GGML_BACKEND_DL=ON`, `GGML_CPU_ALL_VARIANTS=ON` | API llama.cpp đổi nhanh, port vcpkg tụt hậu; một bản phát hành chạy trên mọi CPU và tự chọn AVX2/AVX-512. |
| 4 | SHA-256 khi tải mô hình | **Tự viết** theo FIPS 180-4, kiểm bằng vector thử chuẩn | libcurl trên Windows dùng Schannel, không kèm OpenSSL; tránh phụ thuộc nền tảng. |
| 5 | Font | **Noto Sans** (Regular + Bold) + **ký hiệu toán lấy từ Noto Sans Math** (merge font ImGui); subset glyph khi đóng gói | Đủ tiếng Việt và Hy Lạp; OFL-1.1. |
| 6 | Glossary khởi đầu | Claude soạn bản nháp 30–50 thuật ngữ/lĩnh vực con, cột `source = draft`; người dùng duyệt và đổi thành `verified` | Có dữ liệu để phát triển M5 ngay, người dùng giữ quyền quyết định nội dung. |
| 7 | Toolchain Windows | **MSVC + CMake + Ninja + vcpkg**, triplet **`x64-windows-static`** (CRT tĩnh `/MT`) | Ra một file `.exe` gần như không kèm DLL. |

## Thông tin tham chiếu

- vcpkg baseline: `177e8175121536d414f405ed8b3099b325a42638` (2026-11-11).
- Mô hình: `tencent/HY-MT1.5-1.8B-GGUF`, file Q4_K_M (~1,13 GB). Tham số sinh theo model card: `temperature 0.7`, `top_k 20`, `top_p 0.6`, `repetition_penalty 1.05`. Template EN→VI thuộc nhóm "XX↔XX"; lấy nguyên văn các template (thường / chỉ định thuật ngữ / ngữ cảnh) từ model card khi làm M3.
