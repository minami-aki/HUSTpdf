# Pdfread

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
| M5 | Tầng 1: lĩnh vực, glossary, placeholder, hậu kiểm | chưa |
| M6 | Cache SQLite, tải mô hình, đánh giá, CI | chưa |

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

Mọi file chạy và DLL nằm chung trong `build/<preset>/bin/`: `pdfread.exe`, các DLL của llama.cpp/ggml (`llama.dll`, `ggml*.dll`, mỗi biến thể CPU một `ggml-cpu-*.dll`, ggml tự chọn biến thể tốt nhất lúc chạy), thư mục `data/fonts`.

## Mô hình dịch

Không đóng gói kèm. Tải `HY-MT1.5-1.8B-Q4_K_M.gguf` (1,13 GB) từ [tencent/HY-MT1.5-1.8B-GGUF](https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF) (giấy phép Tencent HY Community License) và đặt vào thư mục `models/` (cạnh `pdfread.exe` hoặc ở gốc dự án khi phát triển; `models/` nằm trong `.gitignore`), hoặc chọn file bằng nút **Mô hình…**. SHA-256: `4383ac0c3c8e476de98ff979c2a3f069f8c4fb385e7860cf2d28da896cc477c7`.

## Chạy

```powershell
.\build\release\bin\pdfread.exe [file.pdf] [--page N] [--zoom 150 | --fit-page] [--timing] [--verbose]
                                [--model file.gguf] [--threads N] [--repack] [--gen-prompt]
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
| | Bôi đen một phần câu: dịch cả câu (tô vàng nhạt trên trang); đổi sang "chỉ phần chọn" ngay trong popup. Một từ / cụm ≤ 4 từ: dịch đúng phần chọn |

Tham số mô hình: `--threads N` số luồng suy luận (mặc định = số nhân vật lý, tối đa 8); `--repack` sắp xếp lại trọng số cho AVX2/AVX-512 (prompt dài xử lý nhanh hơn nhưng tốn thêm ~700 MB RAM); `--gen-prompt` kết thúc prompt bằng `<｜hy_Assistant｜>` thay vì như ví dụ chính thức; `--context` (thử nghiệm) thêm 1 câu trước/sau vào prompt bằng mẫu ngữ cảnh — mặc định tắt vì làm hỏng bản dịch với mô hình 1.8B Q4.

`--timing` ghi thời gian mở file, dựng display list, render từng tile, nạp mô hình, token đầu tiên, token/s ra log (bản Debug có console).

Công cụ kiểm thử:

```powershell
.\build\debug\bin\make_sample_pdf.exe sample.pdf 8          # tạo PDF mẫu 12 trang
.\build\debug\bin\pdfread.exe sample.pdf --snapshot out.png   # chờ render xong, chụp cửa sổ, thoát
# Đặt sẵn vùng chọn (trang từ 1, tọa độ point từ góc trên-trái trang; đơn vị c/w/l):
.\build\debug\bin\pdfread.exe sample.pdf --select 1:72,150:300,200 --snapshot out.png
.\build\debug\bin\pdfread.exe sample.pdf --select 1:72,640:2:200,120 --snapshot out.png   # qua 2 trang
# Dịch thử không cần giao diện (stdin: mỗi dòng một câu):
.\build\release\bin\pdfread-translate.exe --model models\HY-MT1.5-1.8B-Q4_K_M.gguf "The kernel schedules threads."
.\build\release\bin\pdfread-translate.exe --model ... --context "câu xung quanh" --term "stress=ứng suất" "..."
# Test với mô hình thật (mặc định bị ẩn):
$env:PDFREAD_TEST_MODEL = "models\HY-MT1.5-1.8B-Q4_K_M.gguf"; .\build\release\bin\pdfread_tests.exe "[model]"
```
Ở chế độ `--snapshot`, text của vùng chọn và bản dịch (nếu có mô hình) được ghi ra log dạng `SELECTION<<...>>`, `TRANSLATION<<...>>`.

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
tools/          build.ps1, sample_pdf/, translate_cli/, eval/
third_party/    llama.cpp (submodule, cố định v0.9.0)
```

## Giấy phép

[AGPL-3.0](LICENSE) (bắt buộc do dùng MuPDF). Giấy phép của thư viện bên thứ ba: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). File mô hình dịch không đi kèm bản phát hành; người dùng tải riêng và chịu điều khoản giấy phép của mô hình.
