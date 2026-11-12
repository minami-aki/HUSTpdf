#pragma once
// Trạng thái ứng dụng và vòng lặp chính.
//
// Vòng lặp tiết kiệm CPU: sau mỗi sự kiện chỉ vẽ vài frame rồi chờ bằng
// glfwWaitEvents(). Luồng nền (mở file, render tile) đánh thức UI bằng
// glfwPostEmptyEvent() khi có kết quả. App đứng yên thì không vẽ gì.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "pdf/document.hpp"
#include "pdf/render_service.hpp"
#include "ui/texture_cache.hpp"
#include "ui/viewer.hpp"
#include "util/concurrent_queue.hpp"
#include "util/thread_pool.hpp"

struct GLFWwindow;

namespace pdfread::pdf {
class RootContext;
}

namespace pdfread::app {

struct AppOptions {
    std::filesystem::path open_path;      // mở ngay khi khởi động
    std::filesystem::path snapshot_path;  // chế độ kiểm thử: chụp màn hình rồi thoát
    std::optional<int> start_page;        // 0-based
    std::optional<double> zoom;           // 1.0 = 100%
    bool fit_page = false;
    std::filesystem::path font_dir;
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

    static void on_drop(GLFWwindow* window, int count, const char** paths);
    static void on_refresh(GLFWwindow* window);
    static void on_content_scale(GLFWwindow* window, float xscale, float yscale);

    GLFWwindow* window_;
    AppOptions options_;

    ConcurrentQueue<OpenResult> open_results_;
    ThreadPool loader_;  // mở tài liệu ở nền (1 luồng)
    pdf::RenderService render_;
    ui::TextureCache textures_;
    ui::Viewer viewer_;

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
