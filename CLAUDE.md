# Dự án: Trình đọc PDF kỹ thuật có dịch Anh → Việt tức thời (C++ native, offline)

## 1. Bối cảnh và mục tiêu

Ứng dụng desktop đọc PDF dành cho tài liệu khoa học – kỹ thuật. Khi người dùng bôi đen một từ hoặc một câu, app hiện popup bản dịch tiếng Việt gần như ngay lập tức.

- **Chỉ dịch một chiều Anh → Việt.** Không cần ngôn ngữ khác, không cần chiều ngược lại, không thiết kế tham số ngôn ngữ nguồn/đích.
- **Phạm vi nội dung:** giáo trình, bài báo, datasheet, tiêu chuẩn, sổ tay kỹ thuật bậc đại học và sau đại học, bao phủ các khối ngành của Đại học Bách khoa Hà Nội (xem mục 5.1).
- **Chạy hoàn toàn offline** trên máy cá nhân, ưu tiên chạy tốt chỉ với CPU.
- **Tiêu chí số một: nhẹ và nhanh.** Dung lượng cài đặt nhỏ, RAM nền thấp, không tốn CPU khi người dùng không thao tác.

## 2. Tech stack (đã chốt)

| Thành phần | Lựa chọn |
|---|---|
| Ngôn ngữ | C++20 (C cho phần giao tiếp MuPDF) |
| Cửa sổ, input | **GLFW** |
| Đồ họa | **OpenGL 3.3 core** |
| Giao diện | **Dear ImGui** (backend GLFW + OpenGL3), dùng **imgui_freetype** để render font đẹp hơn |
| Hộp thoại mở file | **nativefiledialog-extended** (NFD) |
| PDF | **MuPDF** — cố định một phiên bản (ví dụ 1.28.x), không dùng Qt hay bất kỳ PDF engine nào khác |
| Mô hình dịch chính | **llama.cpp liên kết trực tiếp vào app** (không dùng server/sidecar) + **Tencent HY-MT1.5-1.8B** dạng GGUF (Q4_K_M hoặc tương đương) |
| Mô hình dịch thay thế (giai đoạn sau) | **CTranslate2** + **SentencePiece** (C++) + VinAI Translate en2vi (INT8) |
| Tách câu | Bộ tách câu tự viết theo luật + danh sách viết tắt kỹ thuật (ICU là tùy chọn; cân nhắc vì dữ liệu ICU làm tăng đáng kể dung lượng) |
| Cache, glossary, cài đặt | **SQLite** |
| Tải mô hình lần đầu | **libcurl** |
| Build | **CMake + vcpkg**, GitHub Actions (Windows trước, sau đó Linux/macOS) |
| Giấy phép dự án | **AGPL-3.0** (bắt buộc do dùng MuPDF) |

Không dùng Qt. Không dùng Electron/Tauri hay bất kỳ WebView nào.

## 3. Kiến trúc tổng thể

### 3.1. Các luồng (thread)

```
Main/UI thread     → GLFW + ImGui, hiển thị trang (texture), bắt chuột, vẽ highlight và popup
Render workers     → MuPDF: render trang/tile ra pixmap (mỗi luồng một fz_context riêng)
Text worker        → MuPDF: trích fz_stext_page, làm sạch, tách đoạn/câu, đánh dấu thuật ngữ (tầng 2)
Inference thread   → llama.cpp: nạp mô hình khi mở app, xử lý hàng đợi dịch, stream token về UI
```

Quy tắc bắt buộc:
- **Không chạy tác vụ nặng trên UI thread** (render MuPDF, trích text, suy luận mô hình, I/O lớn).
- Chỉ **một** inference thread. Có vùng chọn mới thì **hủy** yêu cầu đang chạy (dùng `abort_callback` của llama.cpp hoặc cờ kiểm tra giữa các bước sinh token). Người dùng chỉ quan tâm vùng chọn mới nhất.
- Giao tiếp giữa các luồng qua hàng đợi thread-safe. Kết quả trả về UI thread theo kiểu message/queue, UI thread đọc mỗi frame.

### 3.2. Tiết kiệm CPU với ImGui (rất quan trọng)

ImGui là immediate-mode và mặc định vẽ lại liên tục. Để app "nhẹ":
- Khi không có tương tác hay tác vụ nền, dùng `glfwWaitEvents()` hoặc `glfwWaitEventsTimeout()` thay vì vòng lặp vẽ liên tục.
- Khi luồng nền có kết quả mới (trang render xong, token dịch mới), gọi `glfwPostEmptyEvent()` để đánh thức UI vẽ lại.
- Mục tiêu: app đứng yên thì CPU gần 0%.

### 3.3. MuPDF — yêu cầu cụ thể

- **Đa luồng:** `fz_context` không dùng chung giữa các luồng. Tạo context gốc với hàm lock (`fz_locks_context`), mỗi luồng dùng `fz_clone_context`. Mọi lời gọi MuPDF bọc trong `fz_try` / `fz_catch`, và bọc tiếp bằng lớp C++ RAII để không rò rỉ tài nguyên.
- **Render:** tạo **display list** cho mỗi trang (cache), render từ display list ra `fz_pixmap` → upload thành OpenGL texture.
  - Cuộn liên tục (continuous scroll) nhiều trang; chỉ render các trang trong vùng nhìn và lân cận.
  - Zoom, vừa chiều rộng (fit width), vừa trang (fit page).
  - Ở mức zoom lớn, render theo **tile** để không tạo texture quá lớn.
  - Cache texture theo **LRU** có giới hạn bộ nhớ; giải phóng texture của trang ở xa.
  - Hỗ trợ màn hình high-DPI (lấy content scale từ GLFW).
- **Text có cấu trúc:** dùng `fz_stext_page` với cờ `FZ_STEXT_DEHYPHENATE` (bỏ gạch nối cuối dòng). Giữ thông tin khối → dòng → ký tự và tọa độ để ánh xạ vùng chọn ↔ câu ↔ đoạn.
- **Bôi đen:**
  - Kéo chuột: `fz_highlight_selection` để lấy các vùng tứ giác cần tô sáng, `fz_copy_selection` để lấy text UTF-8.
  - Double-click chọn từ, triple-click chọn dòng: `fz_snap_selection` với chế độ tương ứng.
  - Chuyển đổi tọa độ chuẩn xác giữa màn hình ↔ trang (tính cả zoom, scroll, DPI).
- Bố cục nhiều cột: kiểm tra thứ tự đọc của stext; nếu sai thì sắp lại các khối theo cột trước khi tách câu.

### 3.4. Giao diện (Dear ImGui)

- Font: nạp font hỗ trợ **đầy đủ tiếng Việt có dấu** (ví dụ Noto Sans hoặc Be Vietnam Pro, giấy phép OFL, đóng gói kèm app), dùng dải glyph tiếng Việt của ImGui (`GetGlyphRangesVietnamese`) cộng thêm các ký hiệu kỹ thuật thường gặp (Hy Lạp, toán học cơ bản). Dùng imgui_freetype.
- Thanh công cụ tối giản: mở file, zoom, chuyển trang, chọn lĩnh vực (ghi đè), bật/tắt dịch tự động.
- **Popup dịch** hiện gần vùng chọn, không che vùng chọn, tự xuống dòng, hiển thị token theo kiểu streaming. Có nút sao chép bản dịch; với tra từ, hiển thị các nghĩa thay thế.
- Đánh dấu thuật ngữ trong bản dịch nếu vi phạm glossary (xem mục 5.3).
- Màn hình quản lý glossary đơn giản (xem, tìm, sửa, nhập CSV). Lưu ý: nhập tiếng Việt có dấu qua bộ gõ (IME) trong GLFW/ImGui có thể hạn chế. Ưu tiên chức năng **nhập/xuất CSV** để người dùng sửa glossary bằng trình soạn thảo ngoài; kiểm tra và báo cáo mức hỗ trợ IME thực tế trên Windows.

## 4. Mô hình dịch

### 4.1. Interface chung

```cpp
struct TranslateRequest {
    std::string text;                    // câu/đoạn cần dịch (đã thay placeholder)
    std::string context;                 // 1–2 câu xung quanh (có thể rỗng)
    std::vector<GlossaryEntry> terms;    // chỉ các thuật ngữ xuất hiện trong text
    std::vector<DomainScore> domains;    // lĩnh vực của tài liệu/mục
};

class Translator {
public:
    virtual bool supportsGlossary() const = 0;
    virtual bool supportsContext() const = 0;
    // Gọi onToken cho mỗi đoạn kết quả; trả false nếu bị hủy
    virtual bool translate(const TranslateRequest&, std::function<void(std::string_view)> onToken,
                           const std::atomic<bool>& cancel) = 0;
    virtual ~Translator() = default;
};
```

(Đây là phác thảo; được phép điều chỉnh nhưng giữ đúng ý.) Nếu backend không hỗ trợ glossary/ngữ cảnh: tự fallback bằng cách placeholder hóa các thuật ngữ trước khi dịch, rồi thay bằng `term_vi` (hoặc giữ nguyên tiếng Anh nếu `keep_english`) sau khi dịch.

### 4.2. Backend llama.cpp + HY-MT1.5

- Đọc model card HY-MT1.5-1.8B trên Hugging Face và dùng **đúng prompt template chính thức** cho chiều EN→VI, gồm template **chỉ định thuật ngữ** (terminology intervention) và **dịch có ngữ cảnh** (contextual translation). Dùng các tham số sinh mà model card khuyến nghị.
- Nạp mô hình **một lần** khi mở app (trên inference thread, không chặn UI; hiển thị trạng thái "đang nạp mô hình").
- Giới hạn độ dài đầu vào và đầu ra hợp lý cho câu ngắn; dừng khi gặp token kết thúc.
- Đo và log: thời gian tới token đầu tiên, tốc độ token/giây.
- Số luồng CPU cho suy luận: mặc định hợp lý theo số nhân, cho phép chỉnh trong cài đặt.

### 4.3. Quản lý file mô hình

- **Không đóng gói mô hình vào bộ cài.** Lần chạy đầu: hỏi người dùng tải mô hình từ Hugging Face (libcurl, có thanh tiến trình, tải tiếp được khi đứt mạng, kiểm tra SHA-256), **hoặc** chọn file GGUF đã có sẵn trên máy (cho máy không có mạng).
- Lưu đường dẫn mô hình trong cài đặt. Sau lần đầu, app không cần mạng.

## 5. Hệ thống ngữ cảnh 3 tầng

Nguyên tắc cốt lõi: **không đưa cả trang vào mô hình.** "Quét" tài liệu/trang bằng thuật toán nhẹ để trích xuất thông tin (lĩnh vực, thuật ngữ, cấu trúc câu/đoạn). Mô hình chỉ được gọi khi người dùng bôi đen, với prompt ngắn gọn (mục tiêu khoảng 100–300 token đầu vào).

### 5.1. Tầng 1 — Tài liệu (chạy 1 lần khi mở file, cache theo hash file)

**Bảng phân loại lĩnh vực** — lưu ở file cấu hình `data/domains.yaml` (hoặc JSON), không hard-code. Cấu trúc 2 cấp: nhóm → lĩnh vực con. Danh sách khởi đầu:

- **Cơ khí:** cơ khí chế tạo máy, cơ điện tử, cơ học kỹ thuật, kỹ thuật ô tô/động lực, kỹ thuật hàng không, kỹ thuật nhiệt – lạnh, máy và tự động thủy khí
- **Điện – Điện tử:** kỹ thuật điện / hệ thống điện, điện tử – viễn thông, điều khiển – tự động hóa, kỹ thuật y sinh
- **Công nghệ thông tin & Truyền thông:** khoa học máy tính, kỹ thuật máy tính, hệ thống thông tin, khoa học dữ liệu & trí tuệ nhân tạo, an toàn thông tin
- **Toán – Tin:** toán ứng dụng, toán tin, xác suất thống kê, tối ưu
- **Vật lý kỹ thuật:** vật lý kỹ thuật, kỹ thuật hạt nhân, quang học / laser
- **Vật liệu:** kim loại – luyện kim, vật liệu polymer / composite, vật liệu điện tử – nano, dệt – may
- **Hóa & Khoa học sự sống:** kỹ thuật hóa học, hóa học, hóa dược, kỹ thuật sinh học, kỹ thuật thực phẩm, kỹ thuật môi trường
- **Kinh tế – Quản lý:** quản lý công nghiệp, kinh tế công nghiệp, quản trị kinh doanh, tài chính – kế toán

Mỗi lĩnh vực con có: `id`, tên tiếng Việt, tên tiếng Anh, danh sách từ khóa tiếng Anh đặc trưng (để phân loại), đường dẫn file glossary riêng.

**Phân loại lĩnh vực:**
- Dựa trên tiêu đề, tóm tắt, mục lục (outline của MuPDF), vài trang đầu. Phương pháp nhẹ: so khớp từ khóa / TF-IDF với danh sách từ khóa từng lĩnh vực. Không bắt buộc dùng LLM.
- **Đa nhãn:** trả về tối đa 3 lĩnh vực kèm điểm tin cậy (ví dụ cơ điện tử = cơ khí + điều khiển + điện tử).
- Tùy chọn phân loại thêm ở mức **chương/mục** (theo outline) cho giáo trình dài.
- Người dùng **ghi đè** được lĩnh vực; lựa chọn lưu theo file.

**Trích thuật ngữ tự động:** cụm danh từ lặp lại, từ viết tắt (kèm dạng đầy đủ nếu tài liệu có định nghĩa, ví dụ "PID (Proportional–Integral–Derivative)"), xếp hạng theo tần suất và độ đặc thù.

**Glossary:**
- Mỗi lĩnh vực con một file glossary (CSV) với các cột: `term_en, term_vi, keep_english, notes, source`. Nạp vào SQLite khi khởi động hoặc khi file thay đổi.
- Hỗ trợ thuật ngữ **giữ nguyên tiếng Anh** (ví dụ firmware, API, PID controller).
- **Thứ tự ưu tiên khi trùng:** glossary người dùng > glossary của tài liệu > glossary lĩnh vực có điểm cao nhất > glossary các lĩnh vực khác của tài liệu.
- Thuật ngữ tự trích chưa có bản dịch: đánh dấu "chưa xác nhận", không đưa vào prompt.
- Thuật ngữ đa nghĩa theo lĩnh vực (ví dụ kernel, bus, stress, phase, resolution, yield, cell): chọn theo lĩnh vực có điểm cao nhất; nếu không chắc, hiển thị các nghĩa thay thế.

### 5.2. Tầng 2 — Trang (khi trang vào vùng nhìn, KHÔNG gọi mô hình)

- Lấy `fz_stext_page` (đã dehyphenate), chuẩn hóa ký tự ghép (ﬁ, ﬂ...), nối dòng bị ngắt giữa câu, xử lý đúng thứ tự đọc.
- Chia **đoạn và câu**, lưu vị trí (chỉ số ký tự + tọa độ) để ánh xạ vùng bôi đen → câu → đoạn. Không tách câu sai ở: "Fig. 3", "e.g.", "i.e.", "et al.", "Eq. (2)", "No.", "vs.", số thập phân, phiên bản "v1.2", đơn vị.
- Đánh dấu thuật ngữ trong trang khớp với glossary.
- Phát hiện vùng **không được dịch**: công thức, tên biến, đoạn mã, đơn vị đo, ký hiệu.
- Mục tiêu: vài chục mili-giây mỗi trang. Cache theo trang. Cuộn nhanh thì hủy tác vụ của trang đã cuộn qua.

### 5.3. Tầng 3 — Vùng chọn (khi người dùng bôi đen)

- Debounce khoảng 200–300 ms sau khi thả chuột.
- **Phân loại vùng chọn:**
  - Một từ hoặc cụm thuật ngữ ngắn → tra glossary trước, sau đó từ điển offline (giai đoạn sau). Dùng lĩnh vực ở tầng 1 để ưu tiên nghĩa chuyên ngành. Chỉ gọi mô hình nếu không tìm thấy.
  - Câu hoặc đoạn → dịch bằng mô hình.
  - Nửa câu → mặc định dịch cả câu chứa vùng chọn và làm nổi bật phần tương ứng; có tùy chọn chỉ dịch phần chọn.
- **Ghép ngữ cảnh:** lĩnh vực (tầng 1) + **chỉ** các thuật ngữ glossary thực sự xuất hiện trong câu + 1–2 câu trước/sau trong cùng đoạn (tầng 2), có giới hạn token.
- **Placeholder:** thay công thức, mã, biến, đơn vị bằng token giữ chỗ trước khi dịch; khôi phục sau; kiểm tra không token nào bị mất hoặc bị đổi.
- **Hậu kiểm:** kiểm tra các thuật ngữ glossary được dịch đúng như chỉ định; nếu sai, đánh dấu trong popup.
- **Streaming** kết quả vào popup.
- **Cache** bản dịch trong SQLite theo khóa: (câu đã chuẩn hóa + hash ngữ cảnh + phiên bản glossary + tên mô hình).

### 5.4. Tùy chọn giai đoạn sau (chỉ làm khi tôi yêu cầu)

- Tái sử dụng KV cache của llama.cpp: khi người dùng dừng ở một vùng khoảng 1–2 giây, xử lý trước khối ngữ cảnh của đoạn hiện tại (đặt ngữ cảnh ở đầu prompt để dùng lại prefix); hủy khi cuộn đi.
- Dịch trước ở nền các câu trên trang đang đọc khi máy rảnh.
- OCR cho PDF dạng ảnh scan (MuPDF có hỗ trợ OCR qua Tesseract nếu build kèm).
- Backend CTranslate2 + VinAI Translate.

## 6. Cấu trúc mã nguồn (đề xuất, được phép điều chỉnh)

```
src/
  app/            # main loop GLFW + ImGui, quản lý trạng thái, cài đặt
  ui/             # các panel ImGui: viewer, toolbar, popup dịch, glossary
  pdf/            # lớp RAII bọc MuPDF: document, page, display list, render, stext, selection
  text/           # làm sạch text, tách đoạn/câu, ánh xạ vị trí, placeholder
  terminology/    # trích thuật ngữ, glossary, phân loại lĩnh vực
  context/        # context_builder: ghép ngữ cảnh 3 tầng
  translate/      # interface Translator, backend llama.cpp, (sau) CTranslate2
  storage/        # SQLite: cache dịch, glossary, cài đặt
  util/           # thread pool, hàng đợi, log, đo thời gian
data/
  domains.yaml
  glossary/<domain_id>.csv
  fonts/
tests/
tools/eval/       # chương trình đánh giá
```

## 7. Kiểm thử và đo lường

- **Unit test** (GoogleTest hoặc Catch2) cho: làm sạch text, tách câu (gồm các ca viết tắt khó), trích thuật ngữ, placeholder hóa/khôi phục, context_builder (giới hạn token, chỉ chứa thuật ngữ liên quan), ánh xạ tọa độ màn hình ↔ trang.
- **Log thời gian** từng bước: render trang, trích stext, ghép ngữ cảnh, thời gian tới token đầu tiên, tổng thời gian dịch. Có thể bật/tắt.
- Theo dõi RAM và CPU khi app đứng yên.

## 8. Đánh giá chất lượng dịch

Chương trình `tools/eval` chạy trên tập 50–100 câu tôi cung cấp (CSV: câu tiếng Anh, lĩnh vực, ngữ cảnh, bản dịch tham chiếu nếu có), so sánh 3 cấu hình:

1. dịch câu đơn, không ngữ cảnh,
2. + lĩnh vực + glossary,
3. + câu xung quanh.

Báo cáo **theo từng nhóm lĩnh vực**: tỉ lệ thuật ngữ dịch đúng theo glossary, số placeholder lỗi, độ trễ trung bình và p95. Xuất kết quả ra CSV để tôi tự so sánh bản dịch.

## 9. Build và phát hành

- CMake + vcpkg (manifest mode). llama.cpp và MuPDF có thể đưa vào dạng submodule nếu vcpkg không phù hợp; cố định phiên bản.
- GitHub Actions: build và chạy test trên Windows trước; sau đó Linux/macOS. Đóng gói bản phát hành lên GitHub Releases (không kèm file mô hình).
- Dự án phát hành theo **AGPL-3.0**; kèm thông báo giấy phép của các thư viện bên thứ ba (MuPDF, llama.cpp, Dear ImGui, GLFW, SQLite, font...).

## 10. Cách làm việc

1. Trước khi viết code, đọc toàn bộ file này và đưa ra **kế hoạch triển khai** theo từng bước nhỏ, kèm cấu trúc thư mục và các interface chính. Chờ tôi duyệt.
2. Triển khai theo các mốc:
   - **M1:** cửa sổ GLFW + ImGui, mở PDF bằng MuPDF, render và cuộn liên tục, zoom; CPU gần 0% khi đứng yên.
   - **M2:** bôi đen (kéo chuột, double-click, triple-click), tô sáng, lấy text đúng.
   - **M3:** nhúng llama.cpp + HY-MT1.5, dịch vùng chọn, popup streaming, hủy yêu cầu cũ.
   - **M4:** tầng 2 (làm sạch, tách câu, ánh xạ vị trí) + ngữ cảnh 1–2 câu xung quanh.
   - **M5:** tầng 1 (lĩnh vực + glossary) + placeholder + hậu kiểm.
   - **M6:** cache SQLite, tải mô hình lần đầu, chương trình đánh giá, GitHub Actions.
3. Sau mỗi mốc: chạy test, báo ngắn gọn những gì đã xong và những gì còn vướng.
4. Gặp quyết định thiết kế có nhiều phương án hợp lý thì **hỏi tôi**, không tự chọn.
5. Không thêm thư viện ngoài danh sách ở mục 2 mà chưa hỏi tôi.
