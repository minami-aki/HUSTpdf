#pragma once
// Trạng thái ứng dụng và vòng lặp chính.
//
// Vòng lặp tiết kiệm CPU: sau mỗi sự kiện chỉ vẽ vài frame rồi chờ bằng
// glfwWaitEvents(). Luồng nền (mở file, render tile) đánh thức UI bằng
// glfwPostEmptyEvent() khi có kết quả. App đứng yên thì không vẽ gì.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdf/document.hpp"
#include "pdf/render_service.hpp"
#include "pdf/search_service.hpp"
#include "pdf/text_service.hpp"
#include "storage/store.hpp"
#include "terminology/glossary_store.hpp"
#include "terminology/term_extractor.hpp"
#include "text/protect.hpp"
#include "translate/translation_service.hpp"
#include "ui/glossary_window.hpp"
#include "ui/model_window.hpp"
#include "ui/settings_window.hpp"
#include "ui/theme.hpp"
#include "ui/texture_cache.hpp"
#include "ui/translation_popup.hpp"
#include "ui/viewer.hpp"
#include "util/concurrent_queue.hpp"
#include "util/thread_pool.hpp"

struct GLFWwindow;

namespace pdfread::pdf {
class RootContext;
}

namespace pdfread::app {

// Vùng chọn đặt sẵn (chế độ kiểm thử --select).
struct SelectSpec {
    ui::SelectionPoint a;
    ui::SelectionPoint b;
    pdf::SelectUnit unit = pdf::SelectUnit::Chars;
};

struct AppOptions {
    std::filesystem::path open_path;      // mở ngay khi khởi động
    std::filesystem::path snapshot_path;  // chế độ kiểm thử: chụp màn hình rồi thoát
    std::optional<int> start_page;        // 0-based
    std::optional<double> zoom;           // 1.0 = 100%
    bool fit_page = false;
    std::optional<SelectSpec> select;
    std::filesystem::path font_dir;
    std::filesystem::path data_dir;  // chứa domains.json, glossary/

    std::filesystem::path model_path;                     // rỗng: tự tìm trong model_search_dirs
    std::vector<std::filesystem::path> model_search_dirs;
    std::filesystem::path model_download_dir;             // nơi lưu mô hình tải về (cửa sổ "Mô hình dịch")
    int threads = 0;                                      // luồng suy luận, 0 = mặc định
    std::optional<translate::Backend> backend;            // --backend; không có: theo cài đặt
    std::optional<std::string> device;                    // --device: GPU ưa dùng (description)
    bool add_generation_prompt = false;                   // xem translate::ModelConfig
    bool repack_weights = false;                          // xem translate::ModelConfig
    bool include_context = false;                         // thêm câu xung quanh vào prompt (thử nghiệm)
    bool open_glossary = false;                           // mở sẵn cửa sổ glossary
    bool open_model_window = false;                       // mở sẵn cửa sổ "Mô hình dịch"
    bool debug_frames = false;                            // chẩn đoán: ghi thống kê từng khung hình ra log
    std::filesystem::path record_dir;                     // --record-frames: lưu cặp khung hình nghi nhấp nháy
    // Kiểm thử giao diện (--search, --reading-mode, --sidebar): tìm sẵn, chế độ đọc, tab thanh bên.
    std::string search;
    std::optional<ui::ReadingMode> reading_mode;
    std::string sidebar;  // "outline" | "bookmarks" | "search"
    std::optional<std::pair<float, float>> click;  // --click x,y: bấm chuột trái (tọa độ cửa sổ) khi trang đã hiện
    bool use_cache = true;                                // cache bản dịch trong SQLite
    std::filesystem::path database_path;                  // rỗng: local_data_dir()/pdfread.db
};

class App {
public:
    App(GLFWwindow* window, const pdf::RootContext& root, AppOptions options);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Chạy vòng lặp tới khi đóng cửa sổ. Trả mã thoát.
    int run();

    void open_file(const std::filesystem::path& path);

private:
    struct OpenResult {
        std::uint64_t ticket = 0;
        std::filesystem::path path;
        std::shared_ptr<pdf::Document> doc;
        std::vector<translate::DomainScore> domains;  // phân loại tự động (có thể lấy từ cơ sở dữ liệu)
        std::string error;
        std::string fingerprint;      // dấu vân tay file (rỗng nếu không có cơ sở dữ liệu)
        std::string domain_override;  // lĩnh vực người dùng đã chọn cho tài liệu này
        int last_page = 0;            // trang đang đọc lần trước
        std::vector<pdf::OutlineItem> outline;
    };

    bool render_frame();
    bool draw_ui();
    void draw_toolbar();
    void draw_status_bar();
    void handle_shortcuts();
    void poll_open_results();
    void open_dialog();
    void set_status(std::string text, bool is_error);
    void update_snapshot(int fb_width, int fb_height);
    void debug_frame(int fb_width, int fb_height, bool presented);

    void update_translation();
    // force_model: bỏ qua trả lời bằng glossary; bypass_cache: không dùng bản dịch đã lưu.
    void request_translation(bool force_model = false, bool bypass_cache = false);
    void start_translation(translate::TranslateRequest request, bool truncated, ui::TranslationPopup::Scope scope,
                           std::vector<ui::TranslationPopup::TermHint> hints, bool bypass_cache);
    void finish_translation(bool from_model);
    void load_settings();
    void save_reading_position();
    void draw_model_window();

    // --- đọc sách (app_reader.cpp): thanh bên, điều hướng, tìm kiếm, dấu trang, chế độ đọc ---
    enum class SidebarTab { Outline = 0, Bookmarks = 1, Search = 2 };
    void draw_sidebar();
    void draw_outline_tab();
    void draw_bookmarks_tab();
    void draw_search_tab();
    void open_sidebar(SidebarTab tab);
    void navigate_to(ui::Location dest);  // ghi lịch sử rồi chuyển tới
    void go_back();
    void go_forward();
    void follow_link(const pdf::Link& link);
    void start_search();
    void poll_search();
    void select_search_hit(int index, bool record_history);
    void update_search_highlights();
    void add_bookmark();
    void reload_bookmarks();
    void set_reading_mode(ui::ReadingMode mode, bool save);
    [[nodiscard]] int current_outline_item() const;
    [[nodiscard]] std::string section_title(int page) const;
    void handle_reader_shortcuts();
    void start_term_extraction(const std::shared_ptr<pdf::Document>& doc, std::uint64_t ticket);
    void apply_domains();
    [[nodiscard]] std::vector<translate::DomainScore> effective_domains() const;
    [[nodiscard]] std::string domain_name(const std::string& id) const;
    void draw_domain_combo();
    void draw_translation_popup();
    void choose_model_dialog();
    void load_model(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path find_default_model() const;

    static void on_drop(GLFWwindow* window, int count, const char** paths);
    static void on_refresh(GLFWwindow* window);
    static void on_content_scale(GLFWwindow* window, float xscale, float yscale);
    static void on_focus(GLFWwindow* window, int focused);

    GLFWwindow* window_;
    AppOptions options_;

    struct GlossaryLoad {
        std::shared_ptr<const terminology::DomainCatalog> catalog;
        std::unique_ptr<terminology::GlossaryStore> store;  // nullptr: lỗi
        std::string catalog_version;
        std::string error;
    };
    struct ExtractResult {
        std::uint64_t ticket = 0;
        std::vector<terminology::TermCandidate> terms;
    };
    // Bản dịch đang hiển thị: để khôi phục placeholder khi stream và hậu kiểm khi xong.
    struct ActiveTranslation {
        std::uint64_t id = 0;
        std::vector<text::Placeholder> placeholders;
        std::vector<terminology::GlossaryEntry> terms;
        std::unique_ptr<text::PlaceholderStream> stream;
        std::string raw;  // bản dịch thô (còn placeholder)
        std::string cache_key;  // rỗng: không lưu cache
        std::string source;     // văn bản gửi đi dịch (lưu kèm cache để tra cứu)
        std::string original;   // câu gốc trước khi thay placeholder (hậu kiểm mất số)
    };

    std::unique_ptr<storage::Store> store_;  // nullptr nếu không mở được cơ sở dữ liệu
    std::string catalog_version_;            // SHA-256 của domains.json (phân loại đã cache còn dùng được?)
    std::string doc_fingerprint_;            // tài liệu đang mở
    std::filesystem::path model_path_;       // mô hình đang dùng / đang nạp

    std::shared_ptr<const terminology::DomainCatalog> catalog_;  // dùng chung với luồng loader
    std::unique_ptr<terminology::GlossaryStore> glossary_;      // nullptr nếu thiếu data/domains.json
    std::vector<translate::DomainScore> auto_domains_;
    std::string domain_override_;  // rỗng = theo phân loại tự động
    std::vector<terminology::TermCandidate> extracted_terms_;
    bool extracting_ = false;
    std::atomic<std::uint64_t> current_ticket_{0};
    ActiveTranslation active_;

    ConcurrentQueue<GlossaryLoad> glossary_results_;
    // Chỉ luồng loader dùng: bảng lĩnh vực để phân loại khi mở tài liệu.
    std::shared_ptr<const terminology::DomainCatalog> loader_catalog_;
    std::string loader_catalog_version_;
    ConcurrentQueue<OpenResult> open_results_;
    ConcurrentQueue<ExtractResult> extract_results_;
    ThreadPool loader_;  // mở tài liệu, phân loại lĩnh vực, trích thuật ngữ ở nền (1 luồng)
    pdf::RenderService render_;
    pdf::TextService text_service_;
    pdf::SearchService search_;
    translate::TranslationService translation_;
    ui::TextureCache textures_;
    ui::Viewer viewer_;
    ui::TranslationPopup popup_;
    ui::GlossaryWindow glossary_window_;
    ui::ModelWindow model_window_;
    ui::SettingsWindow settings_window_;

    // Đọc sách
    ui::ReadingMode reading_mode_ = ui::ReadingMode::Normal;
    std::vector<pdf::OutlineItem> outline_;
    std::vector<storage::Bookmark> bookmarks_;
    std::vector<ui::Location> back_;
    std::vector<ui::Location> forward_;
    bool sidebar_open_ = false;
    std::optional<SidebarTab> sidebar_select_;
    // Tìm kiếm
    std::string search_query_;
    std::string searched_query_;              // câu đã tìm (Enter lần nữa = kết quả tiếp)
    std::vector<std::string> search_terms_;   // thuật ngữ tiếng Anh khi tìm bằng tiếng Việt
    std::uint64_t search_ticket_ = 0;
    std::vector<pdf::SearchHit> search_hits_;
    int search_current_ = -1;
    int search_start_page_ = 0;
    bool search_running_ = false;
    int search_pages_done_ = 0;
    int search_pages_total_ = 0;
    bool focus_search_ = false;

    bool auto_translate_ = true;
    bool whole_sentence_ = true;  // bôi đen nửa câu -> dịch cả câu (đổi được trong popup)
    std::uint64_t seen_selection_version_ = 0;
    bool seen_dragging_ = false;
    double translate_at_ = 0;  // thời điểm gửi yêu cầu dịch sau debounce (0 = không có)
    std::string model_status_ = "chưa có";

    std::uint64_t frame_ = 0;
    // Chống chớp trắng: khung mới có trang còn trống (tile chưa render) thì giữ khung đầy đủ đang
    // hiển thị thêm tối đa kHoldBlankSeconds thay vì đưa nền trắng lên màn hình.
    bool last_presented_complete_ = false;
    double hold_since_ = 0;
    bool held_frame_ = false;
    // --debug-frames / --record-frames: khung đã hiển thị gần nhất (RGB, từ dưới lên) và độ sáng theo ô lưới.
    std::vector<unsigned char> rec_prev_;
    std::vector<double> rec_prev_grid_;
    int rec_prev_w_ = 0;
    int rec_prev_h_ = 0;
    std::uint64_t rec_prev_frame_ = 0;
    int rec_saved_ = 0;
    double rec_prev_time_ = 0;
    int click_step_ = 0;            // --click: 0 chờ, 1..3 đang bấm, 4 xong
    std::uint64_t click_frame_ = 0;
    float ui_scale_ = 1.0f;
    std::optional<float> pending_scale_;
    bool in_frame_ = false;

    std::uint64_t open_ticket_ = 0;
    bool loading_ = false;
    std::filesystem::path loading_path_;
    bool initial_view_applied_ = false;

    std::string status_;
    bool status_is_error_ = false;
    std::string page_buf_;
    bool page_input_active_ = false;

    int snapshot_stable_frames_ = 0;
    std::optional<int> exit_code_;
    double start_time_ = 0;
};

}  // namespace pdfread::app
