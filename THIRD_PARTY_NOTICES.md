# Thông báo giấy phép bên thứ ba

Pdfread phát hành theo AGPL-3.0. Dự án dùng (hoặc dự kiến dùng) các thành phần dưới đây. Văn bản giấy phép đầy đủ của từng thành phần sẽ được đóng gói kèm bản phát hành (mốc M6).

| Thành phần | Giấy phép | Dùng ở | Trạng thái |
|---|---|---|---|
| MuPDF 1.28.6 | AGPL-3.0 | đọc/render PDF | đang dùng (M1) |
| — font nhúng trong MuPDF (URW base-14, Noto không gồm CJK; build với `TOFU_CJK`) | AGPL-3.0 (URW), OFL-1.1 | font dự phòng khi PDF không nhúng font | đang dùng (M1) |
| — FreeType | FreeType License (FTL) | phụ thuộc của MuPDF, imgui_freetype | đang dùng (M1) |
| — HarfBuzz | MIT ("Old MIT") | phụ thuộc của MuPDF | đang dùng (M1) |
| — zlib | zlib | phụ thuộc của MuPDF | đang dùng (M1) |
| — libjpeg-turbo | IJG + BSD-3-Clause | phụ thuộc của MuPDF | đang dùng (M1) |
| — OpenJPEG | BSD-2-Clause | phụ thuộc của MuPDF | đang dùng (M1) |
| — jbig2dec | AGPL-3.0 | phụ thuộc của MuPDF | đang dùng (M1) |
| — Gumbo | Apache-2.0 | phụ thuộc của MuPDF | đang dùng (M1) |
| — libpng, brotli, bzip2 | libpng, MIT, bzip2 | phụ thuộc của FreeType | đang dùng (M1) |
| GLFW 3.5 | zlib | cửa sổ, input | đang dùng (M1) |
| Dear ImGui 1.93 | MIT | giao diện | đang dùng (M1) |
| nativefiledialog-extended 1.4 | zlib | hộp thoại mở file | đang dùng (M1) |
| Noto Sans, Noto Sans Math (`data/fonts`, kèm `OFL.txt`) | SIL OFL-1.1 | font giao diện | đang dùng (M1) |
| llama.cpp / ggml (v0.9.0) | MIT | suy luận mô hình dịch (DLL cạnh exe) | đang dùng (M3) |
| Microsoft Visual C++ Runtime (`vcruntime140*.dll`, `msvcp140.dll`) | Microsoft VC++ Redistributable license | cần cho các DLL llama.cpp/ggml (build `/MD`) | cần đóng gói app-local hoặc yêu cầu VC Redist (M6) |
| nlohmann/json 3.12 | MIT | đọc `data/domains.json` | đang dùng (M5) |
| SQLite | Public domain | cache, glossary, cài đặt | dự kiến (M6) |
| libcurl | curl (MIT-like) | tải mô hình lần đầu | dự kiến (M6) |
| Catch2 v3 | BSL-1.0 | chỉ dùng cho unit test, không vào bản phát hành | đang dùng (M0) |

## Mô hình dịch

Tencent HY-MT1.5-1.8B (GGUF) **không** được đóng gói trong bản cài. Người dùng tự tải về từ Hugging Face hoặc chọn file có sẵn; việc sử dụng mô hình tuân theo **Tencent HY Community License Agreement** (không áp dụng tại EU, Vương quốc Anh, Hàn Quốc; có chính sách sử dụng được chấp nhận; không dùng đầu ra để cải thiện mô hình AI khác). Khi app tải mô hình hộ người dùng (M6) cần hiển thị và lưu kèm `License.txt` của mô hình.
