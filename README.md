# Pdfread

Trình đọc PDF khoa học – kỹ thuật, dịch Anh → Việt tức thời khi bôi đen. Chạy hoàn toàn offline, C++20 native (GLFW + OpenGL 3.3 + Dear ImGui, MuPDF, llama.cpp + HY-MT1.5-1.8B).

Đặc tả đầy đủ: [CLAUDE.md](CLAUDE.md). Các quyết định thiết kế đã chốt: [docs/DECISIONS.md](docs/DECISIONS.md).

## Trạng thái

| Mốc | Nội dung | Trạng thái |
|---|---|---|
| M0 | Khung dự án: CMake + vcpkg, `util/`, unit test | xong |
| M1 | Cửa sổ, mở PDF, render, cuộn liên tục, zoom | xong |
| M2 | Bôi đen, tô sáng, lấy text | chưa |
| M3 | llama.cpp + HY-MT1.5, popup dịch streaming | chưa |
| M4 | Tầng 2: làm sạch, tách câu, ngữ cảnh | chưa |
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

## Chạy

```powershell
.\build\release\src\pdfread.exe [file.pdf] [--page N] [--zoom 150 | --fit-page] [--timing] [--verbose]
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

`--timing` ghi thời gian mở file, dựng display list, render từng tile ra log (bản Debug có console).

Công cụ kiểm thử:

```powershell
.\build\debug\tests\make_sample_pdf.exe sample.pdf 8          # tạo PDF mẫu 12 trang
.\build\debug\src\pdfread.exe sample.pdf --snapshot out.png   # chờ render xong, chụp cửa sổ, thoát
```

## Cấu trúc thư mục

```
src/
  app/          main loop, trạng thái, cài đặt
  ui/           panel ImGui: viewer, toolbar, popup dịch, glossary
  pdf/          lớp RAII bọc MuPDF
  text/         làm sạch text, tách câu, ánh xạ vị trí, placeholder
  terminology/  trích thuật ngữ, glossary, phân loại lĩnh vực
  context/      ghép ngữ cảnh 3 tầng
  translate/    interface Translator, backend llama.cpp
  storage/      SQLite: cache, glossary, cài đặt
  util/         log, đo thời gian, hàng đợi, thread pool
data/           domains.json, glossary/*.csv, fonts/
tests/          unit test (Catch2)
tools/          build.ps1, eval/
third_party/    llama.cpp (submodule, cố định v0.9.0)
```

## Giấy phép

[AGPL-3.0](LICENSE) (bắt buộc do dùng MuPDF). Giấy phép của thư viện bên thứ ba: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). File mô hình dịch không đi kèm bản phát hành; người dùng tải riêng và chịu điều khoản giấy phép của mô hình.
