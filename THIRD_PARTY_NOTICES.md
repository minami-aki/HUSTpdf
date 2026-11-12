# Thông báo giấy phép bên thứ ba

Pdfread phát hành theo AGPL-3.0. Dự án dùng (hoặc dự kiến dùng) các thành phần dưới đây. Văn bản giấy phép đầy đủ của từng thành phần sẽ được đóng gói kèm bản phát hành (mốc M6).

| Thành phần | Giấy phép | Dùng ở | Trạng thái |
|---|---|---|---|
| MuPDF 1.28.x | AGPL-3.0 | đọc/render PDF | dự kiến (M1) |
| — FreeType | FreeType License (FTL) | phụ thuộc của MuPDF, imgui_freetype | dự kiến (M1) |
| — HarfBuzz | MIT ("Old MIT") | phụ thuộc của MuPDF | dự kiến (M1) |
| — zlib | zlib | phụ thuộc của MuPDF | dự kiến (M1) |
| — libjpeg-turbo | IJG + BSD-3-Clause | phụ thuộc của MuPDF | dự kiến (M1) |
| — OpenJPEG | BSD-2-Clause | phụ thuộc của MuPDF | dự kiến (M1) |
| — jbig2dec | AGPL-3.0 | phụ thuộc của MuPDF | dự kiến (M1) |
| — Gumbo | Apache-2.0 | phụ thuộc của MuPDF | dự kiến (M1) |
| GLFW | zlib | cửa sổ, input | dự kiến (M1) |
| Dear ImGui | MIT | giao diện | dự kiến (M1) |
| nativefiledialog-extended | zlib | hộp thoại mở file | dự kiến (M1) |
| Noto Sans, Noto Sans Math | SIL OFL-1.1 | font giao diện | dự kiến (M1) |
| llama.cpp / ggml | MIT | suy luận mô hình dịch | đã thêm submodule (v0.9.0), dùng từ M3 |
| nlohmann/json | MIT | đọc `data/domains.json`, cài đặt | dự kiến (M5) |
| SQLite | Public domain | cache, glossary, cài đặt | dự kiến (M6) |
| libcurl | curl (MIT-like) | tải mô hình lần đầu | dự kiến (M6) |
| Catch2 v3 | BSL-1.0 | chỉ dùng cho unit test, không vào bản phát hành | đang dùng (M0) |

## Mô hình dịch

Tencent HY-MT1.5-1.8B (GGUF) **không** được đóng gói trong bản cài. Người dùng tự tải về từ Hugging Face hoặc chọn file có sẵn; việc sử dụng mô hình tuân theo giấy phép do Tencent công bố trên model card.
