# Thông báo giấy phép bên thứ ba

HUSTpdf (tên cũ: Pdfread) phát hành theo AGPL-3.0. Dự án dùng các thành phần dưới đây. Bản phát hành (`tools/package.ps1`) chép kèm văn bản giấy phép đầy đủ của từng thành phần vào thư mục `licenses/` (lấy từ file `copyright` của vcpkg, `third_party/llama.cpp/LICENSE`, `data/fonts/OFL.txt`).

| Thành phần | Giấy phép | Dùng ở |
|---|---|---|
| MuPDF 1.28.6 | AGPL-3.0 | đọc/render PDF |
| — font nhúng trong MuPDF (URW base-14, Noto không gồm CJK; build với `TOFU_CJK`) | AGPL-3.0 (URW), OFL-1.1 | font dự phòng khi PDF không nhúng font |
| — FreeType | FreeType License (FTL) | phụ thuộc của MuPDF, imgui_freetype |
| — HarfBuzz | MIT ("Old MIT") | phụ thuộc của MuPDF |
| — zlib 1.3 | zlib | phụ thuộc của MuPDF, FreeType, libcurl |
| — libjpeg-turbo, libspng | IJG + BSD-3-Clause, BSD-2-Clause | phụ thuộc của MuPDF |
| — OpenJPEG | BSD-2-Clause | phụ thuộc của MuPDF |
| — jbig2dec | AGPL-3.0 | phụ thuộc của MuPDF |
| — Gumbo | Apache-2.0 | phụ thuộc của MuPDF |
| — libpng, brotli, bzip2 | libpng, MIT, bzip2 | phụ thuộc của FreeType |
| GLFW 3.5 | zlib | cửa sổ, input |
| Dear ImGui 1.93 | MIT | giao diện |
| nativefiledialog-extended 1.4 | zlib | hộp thoại mở file |
| Noto Sans, Noto Sans Math (`data/fonts`, kèm `OFL.txt`) | SIL OFL-1.1 | font giao diện |
| llama.cpp / ggml (v0.9.0) | MIT | suy luận mô hình dịch (DLL cạnh exe), gồm backend `ggml-vulkan.dll` |
| Vulkan-Headers (biên dịch vào `ggml-vulkan.dll`, từ Vulkan SDK) | Apache-2.0 / MIT | backend GPU Vulkan; lúc chạy dùng `vulkan-1.dll` của driver GPU, không chép kèm |
| NVIDIA CUDA runtime, cuBLAS (`cudart64_*.dll`, `cublas64_*.dll`, `cublasLt64_*.dll`) | NVIDIA CUDA Toolkit EULA (Attachment A, phân phối lại) | **chỉ trong gói bổ sung CUDA** (`HUSTpdf-…-cuda.zip`), kèm `licenses/NVIDIA-CUDA-EULA.txt`; gói chính không có |
| nlohmann/json 3.12 | MIT | đọc `data/domains.json` |
| SQLite 3.54 | Public domain | cài đặt, cache bản dịch, thông tin tài liệu |
| libcurl 8.23 (TLS qua Schannel của Windows) | curl (MIT-like) | tải mô hình lần đầu |
| Microsoft Visual C++ Runtime (`vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`) | Microsoft Visual Studio — "Distributable Code" | cần cho các DLL llama.cpp/ggml (build `/MD`); chép app-local cạnh exe |
| Catch2 v3 | BSL-1.0 | chỉ dùng cho unit test, không vào bản phát hành |

## Dữ liệu

- Biểu tượng `res/HUSTpdf.ico` (nhúng vào `HUSTpdf.exe`): logo Đại học Bách khoa Hà Nội, do chủ dự án cung cấp; các cỡ 16–128 px thu nhỏ từ ảnh 256 px gốc bằng `tools/make_icon.ps1`. Logo không thuộc phạm vi giấy phép AGPL-3.0 của mã nguồn; quyền đối với logo thuộc về nhà trường.

- `data/domains.json`, `data/glossary/*.csv` (`source=draft`): bản nháp của dự án, cùng giấy phép AGPL-3.0.
- `data/glossary/physics.nuclear.csv`, các mục `source=dict`: nhập từ *Từ điển chuyên ngành Vật lý & Kỹ thuật hạt nhân tổng hợp (Anh – Việt)* do chủ dự án cung cấp để phát hành cùng app. Giá trị số trong phần giải thích mang tính tham khảo.

## Mô hình dịch

Tencent HY-MT1.5-1.8B (GGUF) **không** được đóng gói trong bản phát hành. Người dùng tải về từ Hugging Face (app hiển thị giấy phép và chỉ tải sau khi người dùng đồng ý) hoặc chọn file có sẵn; việc sử dụng mô hình tuân theo **Tencent HY Community License Agreement** (không áp dụng tại EU, Vương quốc Anh, Hàn Quốc; có chính sách sử dụng được chấp nhận; không dùng đầu ra để cải thiện mô hình AI khác). Khi app tải mô hình, văn bản giấy phép được lưu cạnh file mô hình (`HY-MT1.5-License.txt`).
