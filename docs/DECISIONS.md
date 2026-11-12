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
| 7 | Toolchain Windows | **MSVC + CMake + Ninja + vcpkg**, triplet **`x64-windows-static`** (CRT tĩnh `/MT`; từ M1 dùng biến thể `x64-windows-static-pdfread`, xem bên dưới) | Ra một file `.exe` gần như không kèm DLL. |

## 2026-11-13 — chi tiết kỹ thuật chốt trong M1 (không đổi đặc tả)

- **ImGui 1.93** tạo glyph theo nhu cầu (từ 1.92), nên không còn dùng `GetGlyphRangesVietnamese`: toàn bộ ký tự trong Noto Sans + Noto Sans Math đều hiển thị được. Port vcpkg bật `wchar32` để có ký hiệu toán ngoài BMP (𝑥, 𝛼…).
- **Context MuPDF theo luồng:** mỗi luồng (UI, render, loader) gắn một `fz_clone_context` vào `thread_local`; các lớp RAII giải phóng bằng context của luồng hiện tại. Mọi truy cập `fz_document` đi qua một mutex; render từ display list chạy song song.
- **Render theo tile 512×512 ở mọi mức zoom** (một đường code duy nhất, chỉ render phần nhìn thấy). Tỉ lệ render lượng tử hóa 1/4096 để khóa cache ổn định. Trong lúc chờ tile ở tỉ lệ mới, tạm phóng to/thu nhỏ tile của tỉ lệ cũ.
- **Cache:** display list LRU 48 trang/tài liệu; texture LRU giới hạn 160 MB (tính RGBA); kho MuPDF 256 MB.
- **Luồng render:** min(3, số luồng phần cứng / 2).
- **"Vừa chiều rộng"/"vừa trang"** khớp theo khổ trang phổ biến nhất trong tài liệu (không phải trang rộng nhất), để vài trang khổ ngang không làm các trang dọc bị thu nhỏ; trang rộng hơn thì cuộn ngang.
- **Zoom 100% = 96 dpi** nhân hệ số DPI màn hình.
- Vòng lặp: `glfwWaitEvents` khi đứng yên, vẽ thêm 2 frame sau mỗi sự kiện; luồng nền đánh thức bằng `glfwPostEmptyEvent`.

## 2026-11-13 — bỏ font CJK nhúng trong MuPDF

- Triplet riêng `triplets/x64-windows-static-pdfread.cmake` (= `x64-windows-static` + cờ theo port), nạp qua `VCPKG_OVERLAY_TRIPLETS` trong `CMakePresets.json`.
- MuPDF build với `TOFU_CJK`: bỏ Source Han và Droid Sans Fallback (~28 MB). Vẫn giữ URW base-14 và các font Noto không phải CJK (Hy Lạp, Cyrillic, ký hiệu, toán).
- Hệ quả chấp nhận: PDF tiếng Trung/Nhật/Hàn không nhúng font sẽ hiện ô vuông.
- Kết quả: `pdfread.exe` (release) giảm từ 39,4 MB xuống 15,75 MB. Phần lớn còn lại là font Noto không phải CJK (~12 MB); có thể bỏ thêm bằng `TOFU_NOTO` nếu cần.

## 2026-11-13 — chi tiết kỹ thuật chốt trong M2

- **Text có cấu trúc** dựng từ display list đã cache (không đọc lại trang) với `FZ_STEXT_DEHYPHENATE | FZ_STEXT_MEDIABOX_CLIP`, trên một luồng nền riêng (`TextService`, 1 luồng), cho các trang đang nhìn và các trang thuộc vùng chọn (tối đa 50). Cache 64 trang trên UI thread.
- **Bỏ gạch nối cuối dòng tự làm:** MuPDF 1.28 chỉ đánh dấu dòng "joined", còn `fz_copy_selection` vẫn giữ gạch nối. Quy tắc trong `pdf::join_hyphenated_lines`: dòng sau bắt đầu bằng chữ thường thì bỏ gạch nối ("inter-national" → "international"); chữ hoa/số thì giữ gạch nối ("Runge-Kutta"); soft hyphen luôn bỏ; gạch đứng sau dấu cách là gạch ngang, giữ nguyên. Hạn chế: "self-⏎contained" bị nối thành "selfcontained" — để M4 xử lý bằng danh sách từ.
- **"Từ"** khi nhấn đúp là theo định nghĩa của MuPDF (ngăn bởi khoảng trắng), nên cụm có gạch nối như "proportional-integral-derivative" là một từ.
- **Vùng chọn nhiều trang:** trang giữa chọn trọn trang bằng điểm nằm trong ký tự đầu/cuối của trang (không dùng góc trang, vì với điểm ngoài mọi dòng MuPDF lấy dòng sau khi hòa khoảng cách → ở trang hai cột góc trên-trái bị hiểu là đầu cột phải).
- Thứ tự đọc dùng nguyên thứ tự khối của MuPDF; kiểm tra/sắp lại bố cục nhiều cột để dành cho M4 (tầng 2).

## 2026-11-13 — chi tiết kỹ thuật chốt trong M3

- **llama.cpp v0.9.0 dạng DLL** (`GGML_BACKEND_DL` bắt buộc `BUILD_SHARED_LIBS`), chuẩn C++17 riêng cho submodule (ở C++20 chuỗi `u8"..."` thành `char8_t`), OpenMP tắt. Mọi exe/DLL ra `build/<preset>/bin`.
- **CRT động `/MD` cho riêng llama.cpp/ggml.** Với `/MT` mỗi DLL có heap riêng mà registry backend của ggml truyền đối tượng C++ giữa các DLL → crash `0xC0000409` ngay khi nạp backend. Phần còn lại của app vẫn `/MT`; giao tiếp với llama chỉ qua C API nên trộn CRT an toàn. Hệ quả: cần `vcruntime140*.dll`/`msvcp140.dll` → **M6 phải đóng gói app-local hoặc yêu cầu VC Redist.**
- **Prompt**: tự định dạng theo chat template trong GGUF (`<｜hy_begin▁of▁sentence｜><｜hy_User｜>…`), mặc định kết thúc bằng `<｜hy_place▁holder▁no▁8｜>` như ví dụ chính thức (`add_generation_prompt=False`); hai cách cho chất lượng tương đương khi thử 5 câu. Dịch thường dùng mẫu tiếng Anh với "Vietnamese"; mẫu thuật ngữ/ngữ cảnh chỉ có bản tiếng Trung nên dùng "越南语". Mẫu gộp thuật ngữ + ngữ cảnh là tự ghép (cần đánh giá ở M6).
- **Tham số sinh** theo model card: top_k 20, top_p 0.6, repetition_penalty 1.05 (64 token gần nhất), temperature 0.7. (Metadata GGUF ghi top_p 0.8 — theo model card.) Không có seed cố định nên mỗi lần dịch có thể hơi khác.
- **Không repack trọng số** (`use_extra_bufts = false`), lô 256 token, ngữ cảnh 2048: RAM riêng của tiến trình dịch ~367 MB thay vì ~1,2 GB; tốc độ sinh ~22 token/s không đổi; token đầu với câu thường 0,2–0,6 s, đoạn 300 token ~3 s. `--repack` để bật lại.
- **Luồng suy luận mặc định** = luồng phần cứng / 2 (6 trên Ryzen 5 4600H), tối đa 8.
- **Debounce** 250 ms sau khi thả chuột; đang kéo thì ẩn popup cũ và hủy yêu cầu. Giới hạn 2000 ký tự mỗi lần dịch; tạm nối các dòng bằng dấu cách (M4 sẽ làm sạch, tách câu).
- Chất lượng ban đầu (chưa có lĩnh vực/glossary): câu thường dịch trôi nhưng hay diễn giải dài; thuật ngữ sai theo lĩnh vực ("stress" → "căng thẳng", "kernel" → "hạt nhân", "data hazard" → "vấn đề") — đúng phần việc của M4/M5.

## Thông tin tham chiếu

- vcpkg baseline: `177e8175121536d414f405ed8b3099b325a42638` (2026-11-11).
- Mô hình: `tencent/HY-MT1.5-1.8B-GGUF`, file Q4_K_M (~1,13 GB). Tham số sinh theo model card: `temperature 0.7`, `top_k 20`, `top_p 0.6`, `repetition_penalty 1.05`. Template EN→VI thuộc nhóm "XX↔XX"; lấy nguyên văn các template (thường / chỉ định thuật ngữ / ngữ cảnh) từ model card khi làm M3.
