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
#include "pdf/text_service.hpp"
#include "terminology/glossary_store.hpp"
#include "terminology/term_extractor.hpp"
#include "text/protect.hpp"
#include "translate/translation_service.hpp"
#include "ui/glossary_window.hpp"
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
    int threads = 0;                                      // luồng suy luận, 0 = mặc định
    bool add_generation_prompt = false;                   // xem translate::ModelConfig
    bool repack_weights = false;                          // xem translate::ModelConfig
    bool include_context = false;                         // thêm câu xung quanh vào prompt (thử nghiệm)
    bool open_glossary = false;                           // mở sẵn cửa sổ glossary
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
        std::vector<translate::DomainScore> domains;  // phân loại tự động
        std::string error;
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

    void update_translation();
    void request_translation(bool force_model = false);
    void finish_translation();
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
    };

    std::shared_ptr<const terminology::DomainCatalog> catalog_;  // dùng chung với luồng loader
    std::unique_ptr<terminology::GlossaryStore> glossary_;      // nullptr nếu thiếu data/domains.json
    std::vector<translate::DomainScore> auto_domains_;
    std::string domain_override_;  // rỗng = theo phân loại tự động
    std::vector<terminology::TermCandidate> extracted_terms_;
    bool extracting_ = false;
    std::atomic<std::uint64_t> current_ticket_{0};
    ActiveTranslation active_;

    ConcurrentQueue<OpenResult> open_results_;
    ConcurrentQueue<ExtractResult> extract_results_;
    ThreadPool loader_;  // mở tài liệu, phân loại lĩnh vực, trích thuật ngữ ở nền (1 luồng)
    pdf::RenderService render_;
    pdf::TextService text_service_;
    translate::TranslationService translation_;
    ui::TextureCache textures_;
    ui::Viewer viewer_;
    ui::TranslationPopup popup_;
    ui::GlossaryWindow glossary_window_;

    bool auto_translate_ = true;
    bool whole_sentence_ = true;  // bôi đen nửa câu -> dịch cả câu (đổi được trong popup)
    std::uint64_t seen_selection_version_ = 0;
    bool seen_dragging_ = false;
    double translate_at_ = 0;  // thời điểm gửi yêu cầu dịch sau debounce (0 = không có)
    std::string model_status_ = "chưa có";

    std::uint64_t frame_ = 0;
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
