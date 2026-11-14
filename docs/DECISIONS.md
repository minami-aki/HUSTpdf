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

## 2026-11-13 — M6 (phần 1): SQLite, tải mô hình, đánh giá

- Người dùng chốt: glossary vẫn là CSV + chỉ mục trong bộ nhớ (SQLite chỉ giữ cài đặt, cache bản dịch, thông tin tài liệu); VC++ runtime đóng gói app-local; mô hình tải về `%LOCALAPPDATA%\Pdfread\models`; tạo tập đánh giá mẫu (`tools/eval/sample.csv`, 32 câu, 8 nhóm).
- Cơ sở dữ liệu `%LOCALAPPDATA%\Pdfread\pdfread.db` (WAL). Khóa cache = SHA-256(prompt gửi mô hình + tên file mô hình + biến thể chat template); prompt đã chứa câu chuẩn hóa, ngữ cảnh và đúng các thuật ngữ được chỉ định, nên sửa thuật ngữ khác không làm mất cache. Lưu bản dịch thô (còn placeholder); chỉ lưu bản dịch không có cảnh báo hậu kiểm; tối đa 20 000 mục (LRU).
- Dấu vân tay tài liệu = SHA-256(kích thước + 1 MiB đầu + 1 MiB cuối), không băm cả file. Phân loại lĩnh vực đã cache gắn với SHA-256 của `domains.json`.
- Bộ tải: `.part` + HTTP Range, tự thử lại 5 lần, kiểm tra dung lượng đĩa, SHA-256 tự viết (`util/sha256`) trước khi đổi tên.

## 2026-11-13 — tinh chỉnh dịch tài liệu hạt nhân (người dùng chốt: bỏ placeholder cho đại lượng)

Tập `tools/eval/nuclear.csv` (40 câu hạt nhân – nguyên tử, có ngữ cảnh), mô hình HY-MT1.5-1.8B Q4_K_M, CPU Ryzen 5 4600H.

| Lần | Cấu hình | Thuật ngữ đúng | Lỗi placeholder | Mất số | Độ trễ TB / p95 |
|---|---|---|---|---|---|
| 1 (glossary 508 mục) | 1 dịch trơn | 31,9 % (38/119) | 3 | – | 3,0 / 4,5 s |
| 1 | 2 + glossary | 100 % (119/119) | 2 | – | 3,2 / 4,5 s |
| cuối | 1 dịch trơn | 29,7 % (41/138) | 3 | 3 | 3,0 / 3,9 s |
| cuối | 2 + glossary | 100 % (138/138) | 1* | 1* | 3,2 / 4,6 s |
| cuối | 3 + ngữ cảnh | 97,8 % | 1 | 3 | 3,4 / 4,5 s |

\* câu "17 × 17", đã sửa sau lần chạy này (3/3 lần chạy lại đúng).

- Lỗi nghĩa thấy khi đọc bản dịch (không phải lỗi thuật ngữ chuyên ngành mà là từ thường bị hiểu sai trong ngữ cảnh hạt nhân): activity → "hoạt động", exposure → "phơi bày", shutdown → "máy móc ngừng hoạt động", rated power → "công suất được đánh giá", magnetic field → "trường từ", transition → "chuyển đổi", enriched → "tinh chế", sublevel → "tầng sublevel". Thêm ~35 mục vào glossary hạt nhân (hoạt độ, chiếu xạ, dừng lò, công suất định mức, từ trường, chuyển dời, làm giàu, phân mức, giam giữ…) và các cụm dài hơn để glossary không cắt mất nghĩa ("effective dose limit" trước đây khớp "effective dose" và mất chữ "giới hạn").
- "binding energy per nucleon" đổi thành "năng lượng liên kết trên mỗi nuclôn": với "năng lượng liên kết riêng", mô hình tự thêm "của mỗi hạt nhân" (sai).
- Thuật ngữ glossary viết thường đứng đầu câu ("dãy Balmer…"): viết hoa chữ đầu bản dịch nếu câu gốc viết hoa (`text::match_initial_case`, xử lý cả chữ có dấu).
- **Lệch đặc tả mục 5.3 (đơn vị):** đo A/B trên 72 câu (tập chung + hạt nhân), cấu hình 2: đại lượng thành placeholder → 3 lỗi placeholder, 4 con số bị mất ("500 h", "4.95%"…); để nguyên trong câu → 1 lỗi, 2 con số mất (đều do "17 × 17"). Mô hình chép nguyên "8.8 MeV", "20 mSv", "662 keV" rất ổn định nhưng hay bỏ rơi token ⟦U…⟧. Người dùng chốt: không thay đại lượng (số + đơn vị, %, tích số thuần như "17 × 17") bằng placeholder; công thức, mã, URL vẫn dùng placeholder. Thay vào đó hậu kiểm "mất số" (`text::missing_numbers`, coi "8.8" = "8,8"): popup cảnh báo "Bản dịch làm mất số: …" và bản dịch đó không vào cache. Bật lại bằng `PlanOptions::protect_quantities` / `pdfread-eval --quantity-placeholders`.
- Bổ sung đơn vị hạt nhân vào bộ nhận diện đại lượng (eV…GeV, Bq, Ci, Gy, Sv, rem, b/barn, u, MWe, GWd/tU…) — dùng khi bật bảo vệ đại lượng.
- Mẫu prompt ngữ cảnh (cấu hình 3) vẫn có lúc dịch câu ngữ cảnh thay cho câu chính (câu #1) — giữ quyết định M4: tắt mặc định.

## 2026-11-13 — từ điển Kỹ thuật hạt nhân Anh–Việt (người dùng chốt cách hiểu và các đề xuất)

- Nguồn: file Markdown của người dùng (3148 mục / 26 phần, giải thích chuyên sâu + công thức LaTeX, phụ lục 147 viết tắt). Đóng gói trong `data/` (phát hành cùng app theo AGPL). Phụ lục A (bảng hằng số, đơn vị, số liệu phân hạch) không nạp.
- Nhập bằng `tools/dict/import_md_dictionary.py` vào `physics.nuclear.csv` (thêm cột `aliases`, `explanation`); mục của từ điển (`source=dict`) thay mục nháp trùng tên hoặc trùng tên gọi khác (229 mục nháp bị thay, 313 giữ lại). Nghĩa tiếng Việt dùng giữa câu: bỏ viết hoa chữ đầu (trừ tên riêng/viết tắt); "Proton = Proton" -> nghĩa "proton".
- Tên thuật ngữ có phần phụ: ký hiệu `($\sigma$)` -> "Ký hiệu: σ" trong giải thích; viết tắt/tên khác trong ngoặc -> tên gọi khác (ghép với ngoặc tương ứng bên tiếng Việt); mô tả có dấu phẩy -> ghi chú; ký hiệu phản ứng `(n,2n)`, `(α,n)` giữ trong tên. "A & B", "A / B", "A, B": chỉ tách khi vế đầu là cụm >= 2 từ và số vế khớp với tiếng Việt; vế sau chỉ thành tên gọi khác nếu là cụm từ hoặc viết tắt (tránh "Toroidal", "Time", "Nuclear" thành thuật ngữ). Một viết tắt dùng cho nhiều mục chỉ giữ ở mục nó thực sự viết tắt (khớp chữ cái đầu/số). Viết tắt ở phụ lục đã là tên gọi khác của thuật ngữ thì không thêm mục riêng.
- LaTeX -> Unicode (ImGui không hiển thị được LaTeX; không thêm thư viện vẽ công thức): chữ Hy Lạp, ký hiệu toán, chỉ số trên/dưới Unicode khi có (ngược lại `_x`, `^(…)`), `\frac` -> a/b, `\sqrt` -> √, dấu mũ/gạch trên bằng ký tự kết hợp; khoảng trắng trong công thức bỏ như LaTeX rồi thêm quanh dấu quan hệ và phép toán hai ngôi; "500 - 700" giữa hai số -> "500–700"; công thức khối `$$…$$` xuống dòng riêng.
- Chỉ mục: mỗi tên gọi khác là một mục ảo (`alias_of`); viết tắt giữ nguyên khi dịch câu nhưng hiển thị nghĩa của thuật ngữ gốc khi tra; trùng tên thì thuật ngữ gốc trước tên gọi khác. Viết tắt toàn chữ hoa khớp phân biệt hoa/thường (trước đó "IT", "USE", "CM" khớp nhầm "it", "use", "cm").
- Tra từ: vùng chọn trùng đúng một mục (tối đa 12 từ) là "thuật ngữ" dù dài hơn 4 từ; vùng chọn được mở rộng ra trọn từ trước khi tra (kéo chuột hụt chữ cái). Giải thích & Công thức chỉ hiện ở popup tra thuật ngữ (vùng cuộn tối đa ~12 dòng, popup rộng 560 px), không hiện khi dịch câu, không vào prompt.
- Glossary (~40 file, 1,2 MB) nạp trên luồng loader thay vì UI thread (đo 140–270 ms); chỉ mục dựng lại ~35–50 ms khi đổi lĩnh vực.
- Thống nhất mục nháp với từ điển: detector -> "detector" (từ điển dùng "detector" ở 28 mục).
- Đánh giá `tools/eval/nuclear.csv` (cấu hình glossary): thuật ngữ đúng 97,9 % (142/145), 0 lỗi placeholder, 0 mất số; 2 lần "hoạt độ phóng xạ" được dịch gọn là "hoạt độ" (nghĩa đúng, hậu kiểm so khớp nguyên văn). Từ điển dùng "năng lượng liên kết riêng" cho binding energy per nucleon — mô hình đôi khi thêm "của mỗi hạt nhân"; giữ theo từ điển của người dùng.

## 2026-11-13 — M6 (phần cuối): đánh giá, tải mô hình thật, đóng gói, CI

- Đánh giá `tools/eval/sample.csv` (32 câu, 4 câu × 8 nhóm), HY-MT1.5-1.8B Q4_K_M, Ryzen 5 4600H, 6 luồng:

  | Cấu hình | Thuật ngữ đúng | Lỗi placeholder | Mất số | Độ trễ TB / p95 |
  |---|---|---|---|---|
  | 1 dịch câu đơn | 36,0 % (32/89) | 0 | 0 | 3,6 / 4,8 s |
  | 2 + lĩnh vực + glossary | 100 % (89/89), mọi nhóm 100 % | 0 | 0 | 3,7 / 4,9 s |
  | 3 + câu xung quanh | 96,6 % (86/89; nhóm vật liệu 80 %) | 0 | 0 | 4,0 / 5,2 s |

  Theo nhóm, dịch trơn chỉ đúng 17–47 % thuật ngữ (cơ khí thấp nhất); glossary trong prompt đưa tất cả lên 100 %. Cấu hình 3 không cải thiện và đôi khi làm sai (như M4) — giữ tắt mặc định.
- Tải tiếp thật từ Hugging Face (test ẩn `[network]`): `.part` thiếu 4 MiB cuối -> yêu cầu Range từ byte 1 128 886 208 (qua chuyển hướng CDN), tải phần thiếu, SHA-256 khớp, tổng 9 s. Tải văn bản giấy phép (`License.txt`) cũng kiểm tra trong `[network]`.
- Đóng gói (`tools/package.ps1`): Visual C++ runtime app-local = đúng các DLL mà file trong gói import (dumpbin): `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll` (các `api-ms-win-crt-*` có sẵn trong Windows 10+). Gói 36 MB (zip 17 MB): `pdfread.exe` 18,2 MB (MuPDF + curl + SQLite tĩnh), llama/ggml ~13 MB (9 biến thể CPU). Giấy phép bên thứ ba lấy từ file `copyright` của vcpkg (21 thành phần, bỏ Catch2). Đã chạy bản đóng gói: tìm thấy `data/`, nạp DLL, dịch đúng.
- CI (`.github/workflows/ci.yml`): `windows-latest`, clone vcpkg (cần lịch sử cho `builtin-baseline`), cache thư mục binary cache theo hash `vcpkg.json` + `triplets/`, `build.ps1 -Preset release -Test`, `package.ps1`, artifact zip; tag `v*` -> `gh release create` kèm zip. Chưa chạy được vì repo chưa có remote GitHub.

## 2026-11-13 — chống chớp trắng khi trang chưa render xong

- Triệu chứng (người dùng báo): cả cửa sổ chớp khi thao tác, với một tài liệu cụ thể (Lamarsh), sau khi kéo cửa sổ sang màn hình khác rồi kéo về. Máy: AMD Radeon tích hợp (app chạy trên card này) + RTX 3050, màn hình laptop 1920×1080 144 Hz và LG 2560×1440, cả hai 100 %.
- Chẩn đoán bằng `--debug-frames` (thống kê nội dung từng khung trước khi swap) trên chính phiên của người dùng: lúc đổi cửa sổ 1200 -> 1920 px, chế độ vừa chiều rộng đổi tỉ lệ 2,39 -> 3,87 nên mọi tile phải render lại; khi lật/cuộn sang trang chưa có tile, app đưa lên màn hình khung có nền trắng (độ sáng TB 253) xen kẽ khung có chữ (240) dù mỗi tile chỉ mất 2–6 ms -> chớp trắng. Loại trừ: bộ đệm texture không loại tile đang hiển thị; đổi màn hình không đổi DPI; nội dung khung và ảnh DWM ổn định khi không thiếu tile.
- Sửa: Viewer báo `has_blank_tiles()` (tile hiển thị chưa có và không có tile tỉ lệ khác để vẽ tạm). Nếu khung đang hiển thị đầy đủ mà khung mới lộ nền trắng thì không swap — giữ khung cũ tối đa 100 ms (chờ bằng `glfwWaitEventsTimeout(4 ms)`, không quay vòng); đang cuộn liên tục (khung trước cũng thiếu) thì vẫn hiển thị. Đo lại (End/Home/PageDown trên Lamarsh): 2 khung trống đều được giữ, 0 khung trắng lên màn hình; CPU đứng yên vẫn 0 ms/10 s.
- Các tham số thử nghiệm `--linger`, `--vsync` (giả thuyết tần số quét thay đổi) đã gỡ; giữ `--debug-frames` để chẩn đoán.

## 2026-11-14 — đổi tên app thành HUSTpdf, icon logo Bách khoa

- Tên hiển thị: cửa sổ "HUSTpdf", file chạy `HUSTpdf.exe` (target CMake vẫn là `pdfread`), thông tin phiên bản trong exe (`src/app/app.rc.in`), gói `HUSTpdf-<ver>-win64.zip`, artifact/release CI. Giữ nguyên tên nội bộ: namespace `pdfread`, tên repo, `vcpkg.json`, các công cụ phát triển `pdfread-translate`, `pdfread-eval`, file `pdfread.db`.
- Icon: người dùng cung cấp `HUST.ico` (một ảnh PNG 256×256). `tools/make_icon.ps1` (System.Drawing của .NET, không thêm thư viện) giữ nguyên ảnh 256 px và thêm các cỡ 16–128 px thu nhỏ bicubic -> `res/HUSTpdf.ico` (9 cỡ). Nhúng vào exe với tên tài nguyên `GLFW_ICON`, GLFW tự dùng cho cửa sổ và thanh tác vụ.
- Thư mục dữ liệu `%APPDATA%\Pdfread`, `%LOCALAPPDATA%\Pdfread` -> `…\HUSTpdf`; lần chạy đầu đổi tên thư mục cũ sang mới nếu thư mục mới chưa có (`migrate_legacy_data_dirs`), đường dẫn mô hình đã lưu nằm trong thư mục cũ được trỏ sang thư mục mới.
- Lưu ý khi thử nghiệm: app Claude chạy dạng gói MSIX nên tiến trình khởi chạy từ đó ghi AppData vào bản ảo (`%LOCALAPPDATA%\Packages\Claude_…\LocalCache\Local`); việc chuyển thư mục thật chỉ xảy ra khi người dùng tự mở app.

## 2026-11-14 — tính năng đọc sách: quyết định và lộ trình (người dùng chốt)

- Ghi chú, highlight, hình vẽ: **lưu riêng trong app** (SQLite, theo dấu vân tay file), không sửa file PDF gốc; có nút **xuất PDF kèm ghi chú** (MuPDF ghi annotation chuẩn vào bản sao).
- Backend dịch: **CPU, Vulkan, CUDA** — người dùng chọn trong Cài đặt (mặc định "tự động": GPU nếu có, lỗi thì quay về CPU). Máy hiện có driver Vulkan và NVIDIA (RTX 3050 4 GB) nhưng **chưa cài Vulkan SDK / CUDA Toolkit** (cần để build các backend này).
- Chế độ màu trang: Bình thường / Sepia / Tối / Ấm; bản đầu chấp nhận ảnh trong trang bị đảo màu ở chế độ Tối.
- Dịch trước trang đang đọc: tùy chọn bật/tắt trong Cài đặt; dùng backend nhanh nhất đang có (GPU nếu có — gần như không tốn CPU; trên CPU chạy luồng ưu tiên thấp, dừng ngay khi người dùng thao tác).
- Lộ trình (thứ tự người dùng đồng ý):
  - **M7** — điều hướng & đọc: thanh bên mục lục, link trong PDF bấm được + Quay lại/Tiến tới, dấu trang; tìm kiếm (luồng nền, tô kết quả, F3; tìm bằng tiếng Việt qua glossary/từ điển); chế độ màu trang (shader) + theme giao diện; cửa sổ **Cài đặt** chung.
  - **M8** — backend GPU: Vulkan + CUDA (DLL backend của llama.cpp, nạp động), chọn trong Cài đặt, tự quay về CPU.
  - **M9** — highlight/gạch chân/ghi chú (kèm lưu bản dịch), bảng ghi chú, xuất Markdown/CSV và xuất PDF có annotation.
  - **M10** — dịch trước trang đang đọc, dịch song song nhiều câu trong một đoạn; in (bản nhanh qua trình in mặc định của Windows).
  - **M11** — vẽ tay (ink), hình khối, tẩy, hoàn tác.

## 2026-11-14 — M7: điều hướng, tìm kiếm, chế độ màu, Cài đặt

- **Mục lục:** `fz_load_outline` duyệt trước-thứ-tự (không đệ quy) thành danh sách phẳng có cấp; thanh bên vẽ cây, tô mục ứng với trang đang đọc, mở sẵn mọi nhánh nếu ≤ 40 mục. Đọc trên luồng mở file.
- **Link:** `fz_load_links` theo trang, cache trong `Document`, trích cùng stext trên text worker (UI thread không gọi MuPDF). Tọa độ quy về point từ góc trên-trái trang. Nhấn mà không kéo (< 4 px) mới kích hoạt link, nên bôi đen trên link vẫn được. Link ngoài chỉ mở `http`, `https`, `mailto` (qua `ShellExecuteW`); các scheme khác bị bỏ qua.
- **Lịch sử:** mỗi lần nhảy (link, mục lục, dấu trang, kết quả tìm) lưu vị trí (trang + y) vào ngăn Quay lại, tối đa 100.
- **Dấu trang:** bảng `bookmarks` trong SQLite (schema v2), theo dấu vân tay file, sắp theo trang rồi y.
- **Tìm kiếm:** một luồng riêng có `fz_context` clone; text mỗi trang được "gập" (chữ thường, cả tiếng Việt/Hy Lạp; khoảng trắng gộp) và cache, nên lần tìm sau chỉ quét chuỗi; chỉ tính khung tô sáng cho trang có kết quả. Kết quả đẩy về UI dần (khi có kết quả hoặc mỗi 150 ms), tìm mới thì hủy lần cũ. Đo: Lamarsh 801 trang, lần đầu 2,45 s (phần lớn là trích text), "delayed neutron" 48 kết quả.
- **Tìm bằng tiếng Việt:** truy vấn có chữ tiếng Việt thì tra ngược glossary (`english_for`: khớp đúng `term_vi` trước, nếu không thì `term_vi` chứa cả cụm theo ranh giới từ), tìm đồng thời tối đa 12 thuật ngữ tiếng Anh. Vd. "độ phản ứng" → "reactivity", 164 kết quả trong Lamarsh.
- **Chế độ màu trang:** dùng `fz_tint_pixmap` (màu chữ/màu giấy) ngay khi render tile thay vì shader — không phải sửa đường vẽ OpenGL; màu là một phần khóa tile, đổi chế độ thì render lại các trang đang thấy. Chế độ Tối đổi luôn theme ImGui sang tối. Ảnh trong trang bị nhuộm màu theo (đã chấp nhận).
- **Vừa rộng với tài liệu nhiều khổ:** khổ tiêu biểu = khổ phổ biến nhất nếu chiếm ≥ 30 % số trang, nếu không thì trung vị rộng/cao (sách scan mỗi trang một khổ — Krane trước đây bị tràn ngang vì chọn nhóm 466 pt trong khi nhiều trang tới 707 pt). Ở chế độ vừa rộng/vừa trang, trang đang đọc được căn giữa theo chiều ngang.
- **Cửa sổ Cài đặt** (Ctrl+,) gộp: Hiển thị (chế độ màu), Dịch (dịch tự động, cả câu, ngữ cảnh), Mô hình dịch (cửa sổ "Mô hình" cũ). Lưu vào bảng settings: `ui.reading_mode`, `ui.sidebar`, `ui.auto_translate`, `ui.whole_sentence`, `ui.include_context`.
- **Kiểm thử:** thêm `--search`, `--reading-mode`, `--sidebar`, `--click x,y` (nạp sự kiện chuột thẳng vào ImGui sau `NewFrame` của backend — PostMessage không dùng được vì backend GLFW đọc vị trí con trỏ thật). 136 unit test (thêm tìm kiếm, mục lục/link/SearchService trên PDF sinh tại chỗ, dấu trang, tra ngược glossary, khổ trang trung vị). CPU khi đứng yên sau khi nạp xong mô hình: 0 %.

## 2026-11-14 — M8: backend GPU (người dùng chốt: cài Vulkan SDK + CUDA Toolkit, CUDA là gói bổ sung riêng, Tự động + chọn GPU trong Cài đặt)

- Bộ công cụ build: Vulkan SDK 1.4.363, CUDA Toolkit 13.5 (cài qua winget). nvcc 13.5 nhận MSVC 14.51 không cần cờ `-allow-unsupported-compiler`. CMake tự bật `PDFREAD_VULKAN` / `PDFREAD_CUDA` khi tìm thấy bộ công cụ và không phải bản Debug; CI (không có SDK) vẫn chỉ build CPU.
- **Nạp DLL có chọn lọc** thay cho `ggml_backend_load_all()`: CPU tự chọn biến thể theo `ggml_backend_score` (như ggml), DLL GPU chỉ nạp khi chế độ cần. Phải nạp CPU **trước** `llama_backend_init()` vì hàm đó tự gọi `ggml_backend_load_all()` khi chưa có backend nào. Kiểm tra: chế độ CPU không có module GPU nào trong tiến trình.
- **Chọn thiết bị** (`choose_device`, có unit test): Tự động = GPU rời CUDA → GPU rời Vulkan → CPU (không tự chọn iGPU: Vulkan trên Radeon Vega của Ryzen 5 4600H chỉ ~15 token/s, ngang CPU, lại chia băng thông RAM). Chọn tường minh thì khớp theo tên GPU (description), không còn thì lấy GPU tốt nhất cùng backend. Chọn CUDA mà thiếu gói → cùng GPU qua Vulkan. GPU nạp lỗi → CPU, hiển thị "CPU (GPU lỗi: …)".
- Lưu cài đặt: `model.backend` (auto|cpu|vulkan|cuda), `model.device` (tên GPU). Dòng lệnh `--backend`, `--device` ghi đè. Danh sách GPU dò trên luồng suy luận khi tab Mô hình dịch được mở (không dò lúc khởi động ở chế độ CPU).
- **GPU nạp không dùng mmap** (`load_mode = NONE`): với mmap, các trang file mô hình (~1 GB) vẫn nằm trong RAM sau khi chép lên VRAM. Đo working set sau khi nạp: Vulkan 1381 → 505 MB, CUDA 1644 → 759 MB (CPU: 341 MB); thời gian nạp Vulkan tăng 1,5 → 2,4 s.
- **Khởi động GPU** sau khi nạp: chạy thử lô 16/40/72/136/256 token + 1 token. Vulkan chọn kernel theo cỡ lô và biên dịch shader lần đầu dùng: không khởi động thì câu có độ dài "mới" đầu tiên chờ 3,4 s. Lần đầu sau khi cài (mỗi file exe, driver NVIDIA cache theo ứng dụng) mất 16–30 s ở nền; các lần sau ~0,4 s. Radeon iGPU không cache (5,6 s mỗi lần) — không ảnh hưởng chế độ Tự động.
- Đo (RTX 3050 Laptop 4 GB, 8 câu kỹ thuật, `tools/bench` tạm trong scratchpad): CPU token đầu ~530 ms, 14 token/s, ~3,5 s/câu; Vulkan RTX ~36 ms, ~90 token/s, ~0,5 s/câu; CUDA ~23 ms, ~96 token/s, ~0,47 s/câu. Đoạn 136 token prompt / 153 token ra trong app: CUDA 1,55 s (CPU ~11 s). CUDA chỉ nhỉnh hơn Vulkan ~5–10 % nhưng không phải biên dịch shader lần đầu.
- Đóng gói: gói chính 17 → 29 MB (thêm `ggml-vulkan.dll` 40 MB). Gói bổ sung CUDA 427 MB zip (562 MB giải nén): `ggml-cuda.dll` + `cublas64_13.dll` + `cublasLt64_13.dll` (cudart liên kết tĩnh), kèm NVIDIA EULA và README; thư mục trùng tên gói chính để giải nén chồng lên. Bản hiện tại build CUDA cho kiến trúc của máy build (sm_86, RTX 30xx) — xem câu hỏi còn mở bên dưới.
- Còn mở: (1) danh sách kiến trúc CUDA cho bản phát hành (thêm sm_75/89/120 làm DLL lớn hơn và build lâu hơn nhiều); (2) giấy phép: phân phối cuBLAS (độc quyền, EULA NVIDIA) cùng app AGPL-3.0 có dùng MuPDF — cần người dùng xem xét trước khi đăng gói CUDA công khai.

## Thông tin tham chiếu

- vcpkg baseline: `177e8175121536d414f405ed8b3099b325a42638` (2026-11-11).
- Mô hình: `tencent/HY-MT1.5-1.8B-GGUF`, file Q4_K_M (~1,13 GB). Tham số sinh theo model card: `temperature 0.7`, `top_k 20`, `top_p 0.6`, `repetition_penalty 1.05`. Template EN→VI thuộc nhóm "XX↔XX"; lấy nguyên văn các template (thường / chỉ định thuật ngữ / ngữ cảnh) từ model card khi làm M3.
