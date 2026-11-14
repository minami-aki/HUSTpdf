# HUSTpdf

*(tên cũ: Pdfread)*

Trình đọc PDF khoa học – kỹ thuật, dịch Anh → Việt tức thời khi bôi đen. Chạy hoàn toàn offline, C++20 native (GLFW + OpenGL 3.3 + Dear ImGui, MuPDF, llama.cpp + HY-MT1.5-1.8B).

Đặc tả đầy đủ: [CLAUDE.md](CLAUDE.md). Các quyết định thiết kế đã chốt: [docs/DECISIONS.md](docs/DECISIONS.md).

## Trạng thái

| Mốc | Nội dung | Trạng thái |
|---|---|---|
| M0 | Khung dự án: CMake + vcpkg, `util/`, unit test | xong |
| M1 | Cửa sổ, mở PDF, render, cuộn liên tục, zoom | xong |
| M2 | Bôi đen, tô sáng, lấy text | xong |
| M3 | llama.cpp + HY-MT1.5, popup dịch streaming | xong |
| M4 | Tầng 2: làm sạch, tách câu, ngữ cảnh | xong (ngữ cảnh viết sẵn nhưng tắt mặc định, xem DECISIONS) |
| M5 | Tầng 1: lĩnh vực, glossary, placeholder, hậu kiểm | xong (glossary 37 lĩnh vực là bản nháp, cần duyệt) |
| M6 | Cache SQLite, tải mô hình, đánh giá, CI, đóng gói | xong (CI chạy khi repo có remote GitHub) |
| M7 | Mục lục, link, Quay lại/Tiến tới, dấu trang, tìm kiếm (cả tiếng Việt), chế độ màu trang, cửa sổ Cài đặt | xong |
| M8 | Dịch bằng GPU: Vulkan, CUDA (gói bổ sung), chọn thiết bị trong Cài đặt, tự quay về CPU | xong |

## Build (Windows)

Cần: Visual Studio Build Tools (MSVC x64), CMake ≥ 3.25, Ninja, Git, vcpkg (đặt biến môi trường `VCPKG_ROOT`).

```powershell
git clone --recurse-submodules <repo>
.\tools\build.ps1 -Preset debug -Test     # cấu hình + build + chạy test
.\tools\build.ps1 -Preset release
```

Script tự nạp môi trường MSVC nếu `cl.exe` chưa có trên PATH. Nếu đang ở "Developer PowerShell for VS" có thể gọi thẳng:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Lần build đầu vcpkg sẽ tải và biên dịch các thư viện phụ thuộc nên chậm (khoảng 15–20 phút, chủ yếu là MuPDF và HarfBuzz); các lần sau dùng binary cache.

Mọi file chạy và DLL nằm chung trong `build/<preset>/bin/`: `HUSTpdf.exe`, các DLL của llama.cpp/ggml (`llama.dll`, `ggml*.dll`, mỗi biến thể CPU một `ggml-cpu-*.dll`, ggml tự chọn biến thể tốt nhất lúc chạy), thư mục `data/fonts`.

## Mô hình dịch

Không đóng gói kèm. Lần chạy đầu (chưa có mô hình) app mở cửa sổ **Mô hình dịch**:

- **Tải từ Hugging Face**: `HY-MT1.5-1.8B-Q4_K_M.gguf` (1,13 GB) từ [tencent/HY-MT1.5-1.8B-GGUF](https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF). App hiện giấy phép Tencent HY Community License, chỉ tải khi bạn đồng ý; lưu vào `%LOCALAPPDATA%\HUSTpdf\models\` (kèm `HY-MT1.5-License.txt`), có thanh tiến trình, tạm dừng / tải tiếp (HTTP Range, kể cả sau khi thoát app), tự thử lại khi rớt mạng, kiểm tra SHA-256 `4383ac0c3c8e476de98ff979c2a3f069f8c4fb385e7860cf2d28da896cc477c7` trước khi dùng.
- **Chọn file GGUF có sẵn** (máy không có mạng).

Đường dẫn mô hình được lưu; sau lần đầu app không cần mạng. Thư mục tìm mô hình tự động: `%LOCALAPPDATA%\HUSTpdf\models`, `models/` cạnh `HUSTpdf.exe`, `models/` ở gốc dự án (khi phát triển; nằm trong `.gitignore`). Tab này (**Cài đặt… > Mô hình dịch**) còn chọn thiết bị suy luận, chỉnh số luồng CPU và xóa cache bản dịch.

### Dịch bằng GPU

**Cài đặt > Mô hình dịch > Thiết bị**: Tự động (mặc định) / CPU / từng GPU tìm thấy (Vulkan, CUDA).

- **Tự động**: GPU rời qua CUDA nếu có gói CUDA, không thì qua Vulkan; máy chỉ có GPU tích hợp thì chạy CPU (đo trên Radeon Vega của Ryzen 5 4600H: Vulkan iGPU không nhanh hơn CPU).
- **Vulkan** có sẵn trong gói chính, chỉ cần driver GPU (NVIDIA, AMD, Intel). **CUDA** cần gói bổ sung `HUSTpdf-…-win64-cuda.zip` (cuBLAS ~430 MB): giải nén vào cùng chỗ với gói chính (tên thư mục trùng nhau) rồi mở lại app. Chọn CUDA mà thiếu gói thì app dùng cùng GPU đó qua Vulkan.
- GPU lỗi khi nạp mô hình (hết VRAM, driver lỗi…) thì tự chuyển về CPU; Cài đặt ghi "Đang chạy trên: …", thanh trạng thái ghi backend đang dùng.
- Chế độ CPU không nạp DLL GPU nào (không đánh thức GPU rời). Danh sách GPU chỉ được dò khi mở tab Mô hình dịch.
- Lần đầu dùng Vulkan sau khi cài app hoặc cập nhật driver, GPU biên dịch shader khi nạp mô hình (RTX 3050: 16–30 s, chạy nền, trạng thái "đang nạp…"); driver lưu lại nên các lần sau chỉ ~0,4 s.

| RTX 3050 Laptop 4 GB / Ryzen 5 4600H, 8 câu kỹ thuật | Token đầu | Tốc độ sinh | 1 câu (~45 token ra) | RAM app sau khi nạp |
|---|---|---|---|---|
| CPU (6 luồng) | ~530 ms | ~14 token/s | ~3,5 s | ~340 MB |
| Vulkan · RTX 3050 | ~36 ms | ~90 token/s | ~0,5 s | ~505 MB |
| CUDA · RTX 3050 | ~23 ms | ~96 token/s | ~0,47 s | ~760 MB |
| Vulkan · Radeon iGPU | ~280 ms | ~15 token/s | ~3,2 s | — |

## Cache và cài đặt

`%LOCALAPPDATA%\HUSTpdf\pdfread.db` (SQLite) lưu:

- **cache bản dịch** — khóa là SHA-256 của đúng prompt gửi mô hình (câu, thuật ngữ chỉ định, ngữ cảnh) + tên mô hình; bôi đen lại câu đã dịch thì hiện ngay (popup có nút **Dịch lại**); chỉ lưu bản dịch không có cảnh báo hậu kiểm; tối đa 20 000 mục;
- **cài đặt** — mô hình, số luồng, "Dịch tự động", dịch cả câu / chỉ phần chọn;
- **theo tài liệu** (nhận diện bằng dấu vân tay file) — lĩnh vực tự phân loại, lĩnh vực bạn chọn, trang đang đọc (mở lại file thì về đúng trang).

Glossary vẫn là các file CSV (xem dưới).

## Lĩnh vực và glossary

- `data/domains.json`: 8 nhóm, 37 lĩnh vực con (tên, từ khóa phân loại, file glossary). Khi mở PDF, app phân loại lĩnh vực (tối đa 3, có độ tin cậy) theo mục lục và 3 trang đầu; đổi được ở ô **Lĩnh vực** trên toolbar.
- Glossary là file CSV UTF-8, cột `term_en,term_vi,keep_english,notes,source` và tùy chọn `aliases` (tên gọi khác, `tên=nghĩa|VIẾT_TẮT`), `explanation` (giải thích & công thức) — sửa bằng Excel/trình soạn thảo được; quay lại app là tự nạp lại:
  - `data/glossary/<lĩnh vực>.csv` — glossary lĩnh vực (hiện là **bản nháp** `source=draft`, ~30 thuật ngữ mỗi lĩnh vực, cần duyệt);
  - `data/glossary/physics.nuclear.csv` — thêm **từ điển Kỹ thuật hạt nhân Anh–Việt** (`source=dict`, 3143 thuật ngữ + 56 viết tắt, ~1260 tên gọi khác, kèm giải thích & công thức), nhập từ file Markdown bằng `tools/dict/import_md_dictionary.py` (công thức LaTeX chuyển sang ký tự Unicode: T₁/₂, σ_f, ²³⁵U, √x; mục của từ điển thay mục nháp trùng tên);
  - `%APPDATA%\HUSTpdf\glossary\user.csv` — glossary của bạn (ưu tiên cao nhất);
  - `<tên file>.glossary.csv` cạnh PDF — glossary của tài liệu.
- Thứ tự ưu tiên: của bạn > tài liệu > lĩnh vực điểm cao nhất > lĩnh vực khác của tài liệu. Thuật ngữ thuộc lĩnh vực ngoài tài liệu chỉ hiện ở "Nghĩa khác" khi tra từ.
- Khi dịch: chỉ các thuật ngữ thực sự có trong câu được đưa vào prompt (mẫu "chỉ định thuật ngữ" của HY-MT1.5); công thức, mã, đại lượng có đơn vị, URL được thay bằng placeholder `⟦F1⟧ ⟦C1⟧ ⟦U1⟧ ⟦L1⟧` rồi khôi phục; popup báo thuật ngữ dịch sai và ký hiệu bị mất.
- Bôi đen một từ/cụm có trong glossary (kể cả thuật ngữ dài như "emergency core cooling system", tên gọi khác, viết tắt như "LOCA", "Am-241"; kéo chuột hụt vài chữ cái vẫn được): hiện nghĩa ngay (không gọi mô hình), kèm **Giải thích & Công thức** nếu có và nghĩa ở lĩnh vực khác. Bôi đen câu/đoạn: chỉ hiện bản dịch — thuật ngữ trong câu vẫn được chỉ định cho mô hình, giải thích không đưa vào prompt.
- Viết tắt toàn chữ hoa (IT, USE, CM…) chỉ khớp khi viết hoa đúng, để "it", "use", "cm" không bị coi là thuật ngữ.

## Chạy

```powershell
.\build\release\bin\HUSTpdf.exe [file.pdf] [--page N] [--zoom 150 | --fit-page] [--timing] [--verbose]
                                [--model file.gguf] [--threads N] [--repack] [--gen-prompt]
                                [--no-cache] [--db file.db]
                                [--search "cụm từ"] [--reading-mode normal|sepia|dark|warm] [--sidebar]
                                [--backend auto|cpu|vulkan|cuda] [--device "tên GPU"]
```

| Phím / thao tác | Tác dụng |
|---|---|
| Ctrl+O, kéo thả file | Mở PDF |
| Lăn chuột, ↑ ↓, PgUp PgDn, Space, Home End | Cuộn |
| ← → | Trang trước / sau (khi không có cuộn ngang) |
| Ctrl + lăn chuột | Zoom quanh vị trí con trỏ |
| Ctrl + / Ctrl − / Ctrl 0 | Phóng to / thu nhỏ / 100% |
| Ctrl 1 / Ctrl 2 | Vừa chiều rộng / vừa trang |
| Kéo chuột giữa | Di chuyển trang |
| Kéo chuột trái | Bôi đen (qua được nhiều trang; kéo ra mép thì tự cuộn) |
| Nhấn đúp / nhấn ba lần | Chọn từ / chọn dòng (giữ chuột kéo tiếp để mở rộng theo từ / dòng) |
| Shift + nhấn | Mở rộng vùng chọn tới điểm nhấn |
| Ctrl+C, Esc | Sao chép vùng chọn, bỏ chọn |
| (thả chuột) / Ctrl+T | Dịch vùng chọn: tự động sau 250 ms nếu bật "Dịch tự động", hoặc thủ công |
| Ctrl+G | Mở cửa sổ glossary (xem, tìm, sửa, nhập/xuất CSV, thuật ngữ trích từ tài liệu) |
| F4, nút ≡ | Ẩn/hiện thanh bên: Mục lục / Dấu trang / Tìm kiếm |
| Nhấn vào link | Đi tới đích (link trong tài liệu) hoặc mở trình duyệt (http, https, mailto) |
| Alt+← / Alt+→, nút chuột Back/Forward | Quay lại / tiến tới vị trí trước khi nhảy (link, mục lục, dấu trang, kết quả tìm) |
| Ctrl+F, F3 / Shift+F3 | Tìm trong toàn tài liệu, kết quả kế tiếp / trước. Gõ tiếng Việt (vd. "độ phản ứng") thì tìm các thuật ngữ tiếng Anh tương ứng trong glossary |
| Ctrl+D | Thêm dấu trang tại vị trí đang đọc |
| F9 | Đổi chế độ màu trang: Bình thường → Sepia → Tối → Ấm |
| Ctrl+, | Cửa sổ Cài đặt (hiển thị, dịch, mô hình dịch) |
| | Bôi đen một phần câu: dịch cả câu (tô vàng nhạt trên trang); đổi sang "chỉ phần chọn" ngay trong popup. Một từ / cụm ≤ 4 từ: dịch đúng phần chọn |

Tham số mô hình: `--threads N` số luồng suy luận (mặc định = số nhân vật lý, tối đa 8); `--repack` sắp xếp lại trọng số cho AVX2/AVX-512 (prompt dài xử lý nhanh hơn nhưng tốn thêm ~700 MB RAM); `--gen-prompt` kết thúc prompt bằng `<｜hy_Assistant｜>` thay vì như ví dụ chính thức; `--context` (thử nghiệm) thêm 1 câu trước/sau vào prompt bằng mẫu ngữ cảnh — mặc định tắt vì làm hỏng bản dịch với mô hình 1.8B Q4.

`--debug-frames` ghi thống kê nội dung từng khung hình (độ sáng, kích thước, khung bị giữ vì trang chưa render xong) để chẩn đoán nhấp nháy. `--timing` ghi thời gian mở file, dựng display list, render từng tile, nạp mô hình, token đầu tiên, token/s ra log (bản Debug có console).

Công cụ kiểm thử:

```powershell
.\build\debug\bin\make_sample_pdf.exe sample.pdf 8          # tạo PDF mẫu 12 trang
.\build\debug\bin\make_sample_pdf.exe nuclear.pdf --text tools\eval\nuclear_sample.txt   # dàn trang file văn bản
# Nhập lại từ điển Markdown vào glossary (giữ các mục khác của file CSV):
python tools\dict\import_md_dictionary.py Tu_dien_Ky_thuat_Hat_nhan.md data\glossary\physics.nuclear.csv --report
.\build\debug\bin\HUSTpdf.exe sample.pdf --snapshot out.png   # chờ render xong, chụp cửa sổ, thoát
# Đặt sẵn vùng chọn (trang từ 1, tọa độ point từ góc trên-trái trang; đơn vị c/w/l):
.\build\debug\bin\HUSTpdf.exe sample.pdf --select 1:72,150:300,200 --snapshot out.png
.\build\debug\bin\HUSTpdf.exe sample.pdf --select 1:72,640:2:200,120 --snapshot out.png   # qua 2 trang
# Dịch thử không cần giao diện (stdin: mỗi dòng một câu):
.\build\release\bin\pdfread-translate.exe --model models\HY-MT1.5-1.8B-Q4_K_M.gguf "The kernel schedules threads."
.\build\release\bin\pdfread-translate.exe --model ... --context "câu xung quanh" --term "stress=ứng suất" "..."
.\build\release\bin\pdfread-translate.exe --list-devices                       # GPU dùng được (Vulkan / CUDA)
.\build\release\bin\pdfread-translate.exe --model ... --backend vulkan --device "NVIDIA GeForce RTX 3050 Laptop GPU" "..."
.\build\release\bin\HUSTpdf.exe sample.pdf --glossary --snapshot out.png   # mở sẵn cửa sổ glossary
.\build\release\bin\HUSTpdf.exe sample.pdf --model-window --snapshot out.png   # mở sẵn Cài đặt ở thẻ "Mô hình dịch"
.\build\release\bin\HUSTpdf.exe book.pdf --sidebar --search "delayed neutron" --reading-mode sepia --snapshot out.png
.\build\release\bin\HUSTpdf.exe paper.pdf --click 302,462 --snapshot out.png   # bấm chuột trái tại tọa độ cửa sổ (pixel)
# Test với mô hình thật / cần mạng (mặc định bị ẩn; "[network]" tải tiếp 4 MB cuối của mô hình từ Hugging Face):
$env:PDFREAD_TEST_MODEL = "models\HY-MT1.5-1.8B-Q4_K_M.gguf"; .\build\release\bin\pdfread_tests.exe "[model]"; .\build\release\bin\pdfread_tests.exe "[network]"
```
Ở chế độ `--snapshot`, text của vùng chọn và bản dịch (nếu có mô hình) được ghi ra log dạng `SELECTION<<...>>`, `TRANSLATION<<...>>`, `SEARCH<<...>>` (số kết quả), `CLICK<<...>>`.

## Đánh giá chất lượng dịch

```powershell
.\build\release\bin\pdfread-eval.exe --model models\HY-MT1.5-1.8B-Q4_K_M.gguf --input tools\eval\sample.csv `
    --out eval_results.csv --summary eval_summary.csv [--configs 123] [--limit N]
```

CSV vào (UTF-8, có tiêu đề): `en, domain, context, reference` (domain = id lĩnh vực con trong `data/domains.json`; context, reference có thể rỗng). So sánh 3 cấu hình: (1) dịch câu đơn, (2) + lĩnh vực + glossary, (3) + câu xung quanh. Báo cáo theo nhóm lĩnh vực: tỉ lệ thuật ngữ dịch đúng theo glossary, số placeholder lỗi, số con số bị mất, độ trễ trung bình và p95; `--out` ghi từng bản dịch để so sánh. Tập mẫu: `tools/eval/sample.csv` (32 câu, 8 nhóm), `tools/eval/nuclear.csv` (40 câu hạt nhân). Kết quả đã đo: [docs/DECISIONS.md](docs/DECISIONS.md).

## Đóng gói và phát hành

```powershell
.\tools\package.ps1 [-Build] [-Tools]     # -> dist\HUSTpdf-<phiên bản>-win64\ và .zip (+ -cuda.zip nếu build có CUDA)
```

### Bộ cài đặt (một file .exe)

```powershell
.\tools\make_installer.ps1 [-Build]      # -> dist\HUSTpdf-<phiên bản>-win64-setup.exe (~420 MB)
```

Cần [Inno Setup 6](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`) và bản Release có Vulkan + CUDA. Bộ cài chứa mọi thứ trừ mô hình dịch (app tự hỏi tải ở lần chạy đầu): CPU (mọi biến thể), Vulkan, CUDA + cuBLAS (thành phần tùy chọn, chọn sẵn; kiểu cài "Gọn" bỏ CUDA, tiết kiệm ~560 MB). Khi cài, chọn **chỉ cho tôi** (không cần quyền admin, `%LOCALAPPDATA%\Programs\HUSTpdf`) hoặc **mọi người dùng** (Program Files). Tùy chọn: biểu tượng màn hình nền, thêm HUSTpdf vào "Open with" cho file PDF (không đổi chương trình mặc định). Gỡ cài đặt xóa thư mục chương trình, giữ dữ liệu người dùng (`%LOCALAPPDATA%\HUSTpdf`: mô hình, cache, cài đặt). Cài im lặng: `HUSTpdf-…-setup.exe /VERYSILENT /CURRENTUSER [/TYPE=compact]`. Giao diện trình cài là tiếng Anh (Inno Setup chưa có bản tiếng Việt chính thức); các lựa chọn riêng của HUSTpdf ghi tiếng Việt.

Build GPU: bản Release tự bật backend Vulkan khi có [Vulkan SDK](https://vulkan.lunarg.com/) (`VULKAN_SDK`) và CUDA khi có CUDA Toolkit (`CUDA_PATH`); tắt bằng `-DPDFREAD_VULKAN=OFF` / `-DPDFREAD_CUDA=OFF`. Bản Debug không build GPU. `PDFREAD_CUDA_ARCHS` (mặc định `native` = GPU của máy build) chọn kiến trúc NVIDIA được biên dịch sẵn. CI không có hai bộ công cụ này nên chỉ build CPU.

Gói chạy không cần cài đặt: `HUSTpdf.exe`, DLL llama.cpp/ggml, Visual C++ runtime (`vcruntime140*.dll`, `msvcp140.dll`, chép app-local nên không cần cài VC Redist), `data/`, `LICENSE.txt`, `THIRD_PARTY_NOTICES.md`, `licenses/` (giấy phép từng thư viện). Không kèm mô hình. `-Tools` thêm `pdfread-translate.exe`, `pdfread-eval.exe`.

GitHub Actions (`.github/workflows/ci.yml`): mỗi push/PR build + chạy test bản release trên Windows, đóng gói và đính kèm zip vào artifact; push tag `v*` thì tạo GitHub Release kèm zip. vcpkg cố định theo `builtin-baseline`, có cache binary (lần đầu ~20 phút).

## Cấu trúc thư mục

```
src/
  app/          main loop, trạng thái, cài đặt
  ui/           panel ImGui: viewer, toolbar, popup dịch, glossary
  pdf/          lớp RAII bọc MuPDF
  text/         làm sạch text, tách câu, ánh xạ vị trí, placeholder
  terminology/  trích thuật ngữ, glossary, phân loại lĩnh vực
  context/      ghép ngữ cảnh 3 tầng
  translate/    interface Translator, prompt HY-MT1.5, backend llama.cpp, luồng suy luận
  storage/      SQLite: cache, glossary, cài đặt
  util/         log, đo thời gian, hàng đợi, thread pool
data/           domains.json, glossary/*.csv, fonts/
models/         file GGUF (không commit)
tests/          unit test (Catch2)
tools/          build.ps1, package.ps1, sample_pdf/, translate_cli/, eval/, dict/
.github/        workflows/ci.yml (build + test + đóng gói trên Windows; tag v* -> GitHub Release)
third_party/    llama.cpp (submodule, cố định v0.9.0)
```

## Giấy phép

[AGPL-3.0](LICENSE) (bắt buộc do dùng MuPDF). Giấy phép của thư viện bên thứ ba: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). File mô hình dịch không đi kèm bản phát hành; người dùng tải riêng và chịu điều khoản giấy phép của mô hình.
