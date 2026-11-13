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

## 2026-11-13 — M4: tầng 2 và ngữ cảnh (người dùng chốt: tắt ngữ cảnh mặc định)

**Đo với HY-MT1.5-1.8B Q4_K_M** (`pdfread-translate --context/--raw`, 4 cặp câu, mỗi cấu hình chạy 2 lần):
- Mẫu ngữ cảnh chính thức (`{context}\n参考上面的信息，…`) hỏng 2/4 câu: dịch luôn câu ngữ cảnh, hoặc **chỉ** dịch câu ngữ cảnh và bỏ mất câu cần dịch. Hai cách viết khác (lệnh tiếng Anh; bọc thẻ `<context>`/`<source>`) còn tệ hơn.
- Dịch một cụm ngắn với cả câu làm ngữ cảnh: 3/5 lần mô hình dịch cả câu thay vì cụm.
- Mẫu "Formatted Translation" (`<sn>…</sn>`, để đánh dấu phần tương ứng khi chọn nửa câu) giữ thẻ 3/4 câu nhưng chất lượng giảm rõ: lẫn tiếng Anh ("For một…"), một câu không dịch, một câu sai ngược nghĩa ("below" → "vượt quá"); token đầu ~1,2 s.

**Quyết định:**
- Mặc định **không** đưa câu xung quanh vào prompt. Code ghép 1–2 câu trước/sau trong cùng đoạn (giới hạn 600 ký tự) đã viết (`context::surrounding_context`), bật bằng `--context`; M6 đo lại bằng `tools/eval` (có thể thử Q8_0 / mô hình lớn hơn).
- Bôi đen **nửa câu** → dịch **cả câu** bằng mẫu thường; trên trang tô vàng nhạt phần được dịch, phần bôi đen vẫn màu xanh; không dùng `<sn>`. Popup có nút "cả câu / chỉ phần chọn".
- **Một từ / cụm ≤ 4 từ** không có dấu kết câu → dịch đúng phần chọn (M5: tra glossary trước).
- Bôi đen lố sang câu bên cạnh < 4 ký tự (ví dụ "The" của câu sau) → bỏ qua câu đó.
- Thực tế: dịch trọn câu đã cải thiện thuật ngữ (ví dụ "von Mises stress" trong câu → "ứng suất von Mises", trong khi chữ "Stress" đứng một mình → "Căng thẳng").

**Tầng 2 (`src/text/`, thuần C++, không phụ thuộc MuPDF):**
- Thứ tự đọc: XY-cut ưu tiên khe dọc (cột) chạy suốt chiều cao nhóm; khi cắt ngang thì gộp các dải liền nhau chỉ có khối hẹp (< 60% bề rộng) để không đọc xen kẽ hai cột tại chỗ khoảng cách đoạn thẳng hàng; khối rộng (tiêu đề, chú thích hình) tách riêng.
- Đoạn: tách khối khi khoảng cách dọc > 0,6 chiều cao dòng, hoặc dòng trước kết thúc câu và ngắn (< 85% bề rộng khối), hoặc dòng sau thụt đầu dòng; nối đoạn qua khối/cột khi đoạn trước không kết thúc câu và đoạn sau bắt đầu bằng chữ thường.
- Làm sạch: tách ký tự ghép (ﬁ ﬂ ﬃ…), khoảng trắng Unicode → ' ', bỏ ký tự vô hình; gạch nối cuối dòng theo cùng quy tắc như M2 (soft hyphen luôn bỏ).
- Tách câu theo luật + danh sách viết tắt; cố ý không coi "etc", "ms", "min", "max", "p", "app", "ex", "est" là viết tắt vì hay đứng cuối câu trong tài liệu kỹ thuật.
- Dựng trên luồng text nền ngay sau stext; đo 0,2–1,2 ms/trang trên tài liệu mẫu.
- Hoãn sang M5: phát hiện vùng không dịch (công thức, mã, đơn vị) và đánh dấu thuật ngữ glossary.

**Bôi đen (sửa thêm):** mọi điểm đưa cho MuPDF được dời vào ký tự gần nhất, đo khoảng cách dọc tới đường giữa dòng với trọng số 10 so với ngang. Lý do: với điểm ở khe giữa hai dòng, MuPDF chọn dòng gần nhất theo chiều dọc và khi hòa lấy dòng sau → ở trang hai cột luôn nhảy sang cột phải (lỗi thấy khi chạy thử). Euclid thuần thì kéo điểm bên phải dòng ngắn lên dòng dài phía trên; đo tới mép khung thì sai vì khung ký tự font Times chồng lên dòng kề.

**Khác:** giới hạn token sinh nâng lên 3× số token prompt + 32 (2× làm cụt bản dịch dài dòng của mô hình).

## 2026-11-13 — M5: tầng 1, glossary, placeholder, hậu kiểm

**Người dùng chốt:** soạn glossary nháp cho đủ 37 lĩnh vực (~30 thuật ngữ mỗi lĩnh vực, `source=draft`); glossary đọc vào bộ nhớ từ CSV (SQLite để M6); glossary của tài liệu là `<tên file>.glossary.csv` đặt cạnh PDF.

**Đo với HY-MT1.5-1.8B Q4_K_M:**
- Mẫu "chỉ định thuật ngữ" chính thức (`参考下面的翻译：… 翻译成 …`): 5 câu × 2 lần, **tuân thủ 100%** thuật ngữ (kể cả `keep_english`), bản dịch sát nghĩa hơn hẳn dịch thường ("Xung đột dữ liệu được giải quyết bằng cách chuyển tiếp, và bộ dự đoán rẽ nhánh…").
- Placeholder: 5 dạng (`⟦X1⟧ {X1} [X1] @@X1@@ <X1>`) đều được giữ 8/8; chọn `⟦X1⟧` (không trùng chữ thật). Mô hình đôi khi bỏ dấu cách trước placeholder → khôi phục theo câu gốc. Thay từ thường bằng placeholder làm mất nghĩa (ví dụ "register" thành "sổ ghi chép") nên chỉ che công thức/mã/đại lượng/URL, không che tên biến đứng riêng.

**Thiết kế:**
- `data/domains.json` (nlohmann/json): 8 nhóm, 37 lĩnh vực con, 20–30 từ khóa mỗi lĩnh vực. Phân loại: Σ w·log2(1+số lần) theo từ khóa (cụm nhiều từ nặng hơn), đầu vào là mục lục (×2) và 3 trang đầu (3 đoạn đầu trang 1 ×3); trả tối đa 3 lĩnh vực có điểm ≥ 35% lĩnh vực cao nhất. Chạy trên luồng loader khi mở file (vài ms). Người dùng đổi được (chưa lưu theo file — M6 với SQLite).
- Glossary: 4 tầng (người dùng > tài liệu > lĩnh vực chính > lĩnh vực phụ) + tầng "ngoài tài liệu" chỉ dùng làm nghĩa khác khi tra từ, không vào prompt. Khớp theo từ (không phân biệt hoa/thường, từ cuối chấp nhận số nhiều), ưu tiên cụm dài nhất, bỏ qua vùng placeholder. Thuật ngữ chưa có bản dịch không vào prompt. Glossary lĩnh vực chỉ đọc; sửa trong app ghi vào glossary người dùng hoặc tài liệu. File đổi trên đĩa được đọc lại khi cửa sổ app được focus lại.
- Prompt khi có thuật ngữ: mẫu chỉ định thuật ngữ chính thức, chỉ các thuật ngữ có trong câu.
- Hậu kiểm: thuật ngữ đã chỉ định không xuất hiện trong bản dịch (so không phân biệt hoa/thường tiếng Việt); placeholder mất / lặp / lạ → hiện trong popup.
- Tra từ: vùng chọn ≤ 4 từ có trong glossary → hiện ngay (không gọi mô hình), kèm nghĩa ở lĩnh vực khác; nút "Dịch bằng mô hình", "Sửa thuật ngữ…".
- Trích thuật ngữ tự động (luồng loader, tối đa 300 trang, display list không vào cache để khỏi đẩy trang đang xem ra): từ viết tắt có định nghĩa ("long form (ABBR)" và ngược lại, khớp chữ cái đầu), từ viết tắt lặp lại, cụm 2–4 từ lặp ≥ 2 lần; bỏ cụm con cùng tần suất. Hiện trong tab "Trích từ tài liệu", thêm vào glossary bằng một nút.
- Phát hiện vùng không dịch (`text/protect`): công thức (token có ký hiệu toán làm nhân, mở rộng qua toán tử/số/tên biến ngắn/ngoặc), đại lượng (số + đơn vị trong danh sách, `25%`, `m/s`), mã (`foo()`, snake_case, camelCase, `0x…`, `::`, `->`), URL/email.
- Gõ tiếng Việt trong cửa sổ glossary: chưa kiểm được (cần thao tác tay với bộ gõ). Dự kiến: Unikey/EVKey (gửi phím) dùng được; bộ gõ TSF của Windows có thể không hiện chữ đang soạn. Luôn có đường sửa CSV bằng Excel rồi quay lại app.

## Thông tin tham chiếu

- vcpkg baseline: `177e8175121536d414f405ed8b3099b325a42638` (2026-11-11).
- Mô hình: `tencent/HY-MT1.5-1.8B-GGUF`, file Q4_K_M (~1,13 GB). Tham số sinh theo model card: `temperature 0.7`, `top_k 20`, `top_p 0.6`, `repetition_penalty 1.05`. Template EN→VI thuộc nhóm "XX↔XX"; lấy nguyên văn các template (thường / chỉ định thuật ngữ / ngữ cảnh) từ model card khi làm M3.
