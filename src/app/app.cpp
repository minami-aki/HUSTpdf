#include "app/app.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nfd.h>

#include "pdf/context.hpp"
#include "translate/llama_translator.hpp"
#include "ui/fonts.hpp"
#include "util/log.hpp"
#include "util/utf8.hpp"

namespace pdfread::app {
namespace {

constexpr double kSnapshotTimeoutSeconds = 60.0;
constexpr double kTranslateDebounceSeconds = 0.25;  // chờ sau khi thả chuột rồi mới dịch
constexpr std::size_t kMaxTranslateChars = 2000;    // giới hạn độ dài đoạn gửi đi dịch

App* app_from(GLFWwindow* window) { return static_cast<App*>(glfwGetWindowUserPointer(window)); }

std::string file_name_utf8(const std::filesystem::path& p) { return path_to_utf8(p.filename()); }

}  // namespace

App::App(GLFWwindow* window, const pdf::RootContext& root, AppOptions options)
    : window_(window),
      options_(std::move(options)),
      loader_(ThreadPool::Options{
          .threads = 1,
          .name = "loader",
          .on_start = [&root](std::size_t) { pdf::attach_thread(root); },
          .on_stop = [](std::size_t) { pdf::detach_thread(); },
      }),
      render_(root, 0, [] { glfwPostEmptyEvent(); }),
      text_service_(root, [] { glfwPostEmptyEvent(); }),
      translation_([](const translate::ModelConfig& c) { return std::make_unique<translate::LlamaTranslator>(c); },
                   [] { glfwPostEmptyEvent(); }),
      viewer_(render_, text_service_, textures_) {
    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, &App::on_drop);
    glfwSetWindowRefreshCallback(window_, &App::on_refresh);
    glfwSetWindowContentScaleCallback(window_, &App::on_content_scale);

    float xs = 1.0f;
    float ys = 1.0f;
    glfwGetWindowContentScale(window_, &xs, &ys);
    ui_scale_ = std::max(1.0f, xs);
    ui::apply_style(ui_scale_);

    if (options_.fit_page) viewer_.set_zoom_mode(ui::ZoomMode::FitPage);
    if (options_.zoom) viewer_.set_zoom(*options_.zoom);
    if (!options_.open_path.empty()) open_file(options_.open_path);

    // Nạp mô hình dịch ngay khi mở app (trên luồng suy luận, không chặn UI).
    const std::filesystem::path model = options_.model_path.empty() ? find_default_model() : options_.model_path;
    if (!model.empty()) {
        load_model(model);
    } else {
        log::info("Chưa có mô hình dịch: chọn file GGUF bằng nút \"Mô hình…\"");
    }

    start_time_ = glfwGetTime();
}

App::~App() {
    glfwSetWindowUserPointer(window_, nullptr);
    glfwSetDropCallback(window_, nullptr);
    glfwSetWindowRefreshCallback(window_, nullptr);
    glfwSetWindowContentScaleCallback(window_, nullptr);
    loader_.shutdown();
    viewer_.set_document(nullptr);
}

int App::run() {
    const bool snapshot_mode = !options_.snapshot_path.empty();
    int frames_left = 3;
    while (!glfwWindowShouldClose(window_) && !exit_code_) {
        if (frames_left > 0 || snapshot_mode) {
            glfwPollEvents();
        } else {
            // Đứng yên: chờ sự kiện. Thức dậy đúng hẹn nếu đang đếm debounce trước khi dịch,
            // hoặc định kỳ cho con trỏ nhấp nháy khi đang gõ chữ.
            double timeout = -1;
            if (translate_at_ > 0) timeout = std::max(0.0, translate_at_ - glfwGetTime());
            if (ImGui::GetIO().WantTextInput) timeout = timeout < 0 ? 0.5 : std::min(timeout, 0.5);
            if (timeout >= 0) glfwWaitEventsTimeout(timeout);
            else glfwWaitEvents();
            frames_left = 2;  // ImGui cần thêm một frame để cập nhật trạng thái hover/active
        }
        const bool busy = render_frame();
        frames_left = busy ? 2 : frames_left - 1;
    }
    return exit_code_.value_or(0);
}

bool App::render_frame() {
    if (in_frame_) return false;
    in_frame_ = true;

    if (pending_scale_) {
        ui_scale_ = *pending_scale_;
        pending_scale_.reset();
        ui::apply_style(ui_scale_);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    const bool busy = draw_ui();
    ImGui::Render();

    int fb_w = 0;
    int fb_h = 0;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.96f, 0.96f, 0.97f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!options_.snapshot_path.empty()) update_snapshot(fb_w, fb_h);
    glfwSwapBuffers(window_);

    ++frame_;
    in_frame_ = false;
    return busy;
}

bool App::draw_ui() {
    poll_open_results();
    handle_shortcuts();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    draw_toolbar();

    const float status_h = ImGui::GetFrameHeight() + 2.0f * ui_scale_;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y = std::max(1.0f, avail.y - status_h);
    const float fb_scale = ImGui::GetIO().DisplayFramebufferScale.x;
    bool busy = viewer_.draw(avail, ui_scale_, fb_scale > 0 ? fb_scale : 1.0f, frame_);

    if (!viewer_.document()) {
        const char* hint = loading_ ? "Đang mở tài liệu…" : "Mở file PDF (Ctrl+O) hoặc kéo thả file vào cửa sổ";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        const ImVec2 min = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + (avail.x - ts.x) / 2, min.y + (avail.y - ts.y) / 2),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
    }

    draw_status_bar();
    ImGui::End();

    update_translation();
    draw_translation_popup();
    return busy;
}

// --- dịch ----------------------------------------------------------------------

void App::update_translation() {
    using Kind = translate::TranslationEvent::Kind;
    for (const translate::TranslationEvent& e : translation_.take_events()) {
        switch (e.kind) {
            case Kind::ModelLoading: model_status_ = "đang nạp…"; break;
            case Kind::ModelReady: model_status_ = e.text; break;
            case Kind::ModelFailed:
                model_status_ = "lỗi";
                set_status(std::format("Không nạp được mô hình dịch: {}", e.text), true);
                break;
            default: break;
        }
        popup_.on_event(e);
    }

    // Vùng chọn đổi: đang kéo thì ẩn bản dịch cũ; thả chuột xong thì hẹn giờ dịch.
    const std::uint64_t version = viewer_.selection_version();
    const bool dragging = viewer_.selection_dragging();
    if (version != seen_selection_version_ || dragging != seen_dragging_) {
        seen_selection_version_ = version;
        seen_dragging_ = dragging;
        translate_at_ = 0;
        if (!viewer_.has_selection() || dragging) {
            if (popup_.visible()) {
                popup_.hide();
                translation_.cancel();
            }
        } else if (auto_translate_) {
            translate_at_ = glfwGetTime() + kTranslateDebounceSeconds;
        }
    }
    if (translate_at_ > 0 && glfwGetTime() >= translate_at_) {
        translate_at_ = 0;
        request_translation();
    }
}

void App::request_translation() {
    if (!viewer_.has_selection()) return;
    // Text bôi đen giữ nguyên xuống dòng của PDF; tạm nối thành một dòng (M4 sẽ làm
    // sạch và tách câu đúng cách).
    std::string text = viewer_.selection_text();
    std::replace(text.begin(), text.end(), '\n', ' ');
    const bool truncated = utf8_length(text) > kMaxTranslateChars;
    if (truncated) text = utf8_truncate(text, kMaxTranslateChars);

    translate::TranslateRequest request;
    request.text = std::move(text);
    const std::uint64_t id = translation_.submit(std::move(request));
    popup_.show(id, truncated);
}

void App::draw_translation_popup() {
    if (!popup_.visible()) return;
    const auto& anchor = viewer_.selection_screen_rect();
    if (!anchor) return;  // vùng chọn đang ở ngoài vùng nhìn
    if (!popup_.draw(*anchor, viewer_.viewport_rect(), ui_scale_, translation_.model_state())) {
        translation_.cancel();
    }
}

void App::choose_model_dialog() {
    nfdu8char_t* out = nullptr;
    const nfdu8filteritem_t filters[] = {{"Mô hình GGUF", "gguf"}};
    const nfdresult_t result = NFD_OpenDialogU8(&out, filters, 1, nullptr);
    if (result == NFD_OKAY) {
        load_model(path_from_utf8(out));
        NFD_FreePathU8(out);
    }
}

void App::load_model(const std::filesystem::path& path) {
    translate::ModelConfig config;
    config.model_path = path;
    config.threads = options_.threads;
    config.add_generation_prompt = options_.add_generation_prompt;
    config.repack_weights = options_.repack_weights;
    translation_.load_model(std::move(config));
}

std::filesystem::path App::find_default_model() const {
    std::filesystem::path fallback;
    for (const std::filesystem::path& dir : options_.model_search_dirs) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".gguf") continue;
            if (path_to_utf8(entry.path().filename()).find("HY-MT") != std::string::npos) return entry.path();
            if (fallback.empty()) fallback = entry.path();
        }
    }
    return fallback;
}

void App::draw_toolbar() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float pad = 6.0f * ui_scale_;
    const float height = ImGui::GetFrameHeight() + 2 * pad;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * ui_scale_, 0));
    ImGui::BeginChild("##toolbar", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::Button("Mở…")) open_dialog();
    ImGui::SetItemTooltip("Mở file PDF (Ctrl+O)");

    const bool has_doc = viewer_.document() != nullptr;
    const int count = viewer_.page_count();
    const int current = viewer_.current_page();

    ImGui::SameLine(0, 18.0f * ui_scale_);
    ImGui::BeginDisabled(!has_doc);

    ImGui::BeginDisabled(current <= 0);
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) viewer_.go_to_page(current - 1);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Trang trước");

    ImGui::SameLine();
    if (!page_input_active_) page_buf_ = has_doc ? std::to_string(current + 1) : std::string();
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%s", page_buf_.c_str());
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("00000").x + style.FramePadding.x * 2);
    if (ImGui::InputText("##page", buf, sizeof(buf),
                         ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
        int n = 0;
        const char* end = buf + std::strlen(buf);
        if (std::from_chars(buf, end, n).ec == std::errc{}) viewer_.go_to_page(n - 1);
    }
    page_buf_ = buf;
    page_input_active_ = ImGui::IsItemActive();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(has_doc ? std::format("/ {}", count).c_str() : "/ –");

    ImGui::SameLine();
    ImGui::BeginDisabled(current + 1 >= count);
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) viewer_.go_to_page(current + 1);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Trang sau");

    ImGui::SameLine(0, 18.0f * ui_scale_);
    if (ImGui::Button(" − ")) viewer_.zoom_out();
    ImGui::SetItemTooltip("Thu nhỏ (Ctrl+−)");
    ImGui::SameLine();

    const ui::ZoomMode mode = viewer_.zoom_mode();
    const std::string preview = std::format("{:.0f}%", viewer_.zoom() * 100.0);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Vừa chiều rộng").x + style.FramePadding.x * 2 +
                            ImGui::GetFrameHeight());
    if (ImGui::BeginCombo("##zoom", preview.c_str())) {
        if (ImGui::Selectable("Vừa chiều rộng", mode == ui::ZoomMode::FitWidth)) {
            viewer_.set_zoom_mode(ui::ZoomMode::FitWidth);
        }
        if (ImGui::Selectable("Vừa trang", mode == ui::ZoomMode::FitPage)) {
            viewer_.set_zoom_mode(ui::ZoomMode::FitPage);
        }
        ImGui::Separator();
        for (int pct : {50, 75, 100, 125, 150, 200, 300, 400}) {
            const bool selected = mode == ui::ZoomMode::Custom && std::abs(viewer_.zoom() * 100 - pct) < 0.5;
            if (ImGui::Selectable(std::format("{}%", pct).c_str(), selected)) viewer_.set_zoom(pct / 100.0);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(" + ")) viewer_.zoom_in();
    ImGui::SetItemTooltip("Phóng to (Ctrl++)");

    ImGui::SameLine();
    auto mode_button = [&](const char* label, ui::ZoomMode m, const char* tip) {
        const bool active = mode == m;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) viewer_.set_zoom_mode(m);
        if (active) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tip);
    };
    mode_button("Vừa rộng", ui::ZoomMode::FitWidth, "Vừa chiều rộng cửa sổ");
    ImGui::SameLine();
    mode_button("Vừa trang", ui::ZoomMode::FitPage, "Hiện trọn một trang");

    ImGui::EndDisabled();

    ImGui::SameLine(0, 18.0f * ui_scale_);
    ImGui::Checkbox("Dịch tự động", &auto_translate_);
    ImGui::SetItemTooltip("Dịch ngay khi bôi đen xong. Tắt thì dịch bằng Ctrl+T.");
    ImGui::SameLine();
    if (ImGui::Button("Mô hình…")) choose_model_dialog();
    ImGui::SetItemTooltip("Chọn file mô hình dịch (GGUF)");
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void App::draw_status_bar() {
    const float pad = 8.0f * ui_scale_;
    ImGui::SetCursorPosX(pad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f * ui_scale_);
    ImGui::AlignTextToFramePadding();
    if (viewer_.has_selection()) {
        const std::string& text = viewer_.selection_text();
        ImGui::TextUnformatted(std::format("Đã chọn {} ký tự: “{}”   ·   Ctrl+C: sao chép", utf8_length(text),
                                           utf8_preview(text, 70))
                                   .c_str());
    } else if (!status_.empty()) {
        if (status_is_error_) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.1f, 0.1f, 1.0f));
        ImGui::TextUnformatted(status_.c_str());
        if (status_is_error_) ImGui::PopStyleColor();
    }
    {
        std::string right = std::format("Mô hình: {}", model_status_);
        if (viewer_.document()) {
            right = std::format("Trang {} / {}   ·   {:.0f}%   ·   {}", viewer_.current_page() + 1,
                                viewer_.page_count(), viewer_.zoom() * 100.0, right);
        }
        const float w = ImGui::CalcTextSize(right.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetWindowWidth() - w - pad, ImGui::GetCursorPosX() + pad));
        ImGui::TextUnformatted(right.c_str());
    }
}

void App::handle_shortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) open_dialog();
    if (!viewer_.document()) return;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_T)) request_translation();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C) && viewer_.has_selection()) {
        ImGui::SetClipboardText(viewer_.selection_text().c_str());
        log::debug("Đã sao chép {} ký tự", utf8_length(viewer_.selection_text()));
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)) {
        viewer_.zoom_in();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract)) {
        viewer_.zoom_out();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0)) viewer_.set_zoom(1.0);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_1)) viewer_.set_zoom_mode(ui::ZoomMode::FitWidth);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_2)) viewer_.set_zoom_mode(ui::ZoomMode::FitPage);
}

void App::open_dialog() {
    nfdu8char_t* out = nullptr;
    const nfdu8filteritem_t filters[] = {{"Tài liệu PDF", "pdf"}};
    const nfdresult_t result = NFD_OpenDialogU8(&out, filters, 1, nullptr);
    if (result == NFD_OKAY) {
        open_file(path_from_utf8(out));
        NFD_FreePathU8(out);
    } else if (result == NFD_ERROR) {
        set_status(std::format("Lỗi hộp thoại mở file: {}", NFD_GetError()), true);
    }
}

void App::open_file(const std::filesystem::path& path) {
    const std::uint64_t ticket = ++open_ticket_;
    loading_ = true;
    loading_path_ = path;
    set_status(std::format("Đang mở {}…", file_name_utf8(path)), false);
    loader_.post([this, path, ticket] {
        OpenResult r{.ticket = ticket, .path = path, .doc = nullptr, .error = {}};
        try {
            r.doc = pdf::Document::open(path);
        } catch (const std::exception& e) {
            r.error = e.what();
        }
        open_results_.push(std::move(r));
        glfwPostEmptyEvent();
    });
}

void App::poll_open_results() {
    for (OpenResult& r : open_results_.drain()) {
        if (r.ticket != open_ticket_) continue;  // người dùng đã mở file khác
        loading_ = false;
        if (!r.doc) {
            log::error("Mở '{}' thất bại: {}", path_to_utf8(r.path), r.error);
            set_status(std::format("Không mở được {}: {}", file_name_utf8(r.path), r.error), true);
            continue;
        }
        viewer_.set_document(r.doc);
        set_status(file_name_utf8(r.path), false);
        glfwSetWindowTitle(window_, std::format("{} — Pdfread", file_name_utf8(r.path)).c_str());
        if (!initial_view_applied_) {
            initial_view_applied_ = true;
            if (options_.start_page) viewer_.go_to_page(*options_.start_page);
            if (options_.select) viewer_.select(options_.select->a, options_.select->b, options_.select->unit);
        }
    }
}

void App::set_status(std::string text, bool is_error) {
    status_ = std::move(text);
    status_is_error_ = is_error;
}

void App::update_snapshot(int fb_width, int fb_height) {
    // Chụp khi tài liệu đã mở và mọi tile nhìn thấy đã render xong, ổn định vài frame.
    // Có vùng chọn đặt sẵn và có mô hình: chờ dịch xong.
    const bool waiting_translation =
        options_.select && translation_.model_state() != translate::ModelState::None &&
        (translate_at_ > 0 || popup_.busy() || (auto_translate_ && viewer_.has_selection() && !popup_.visible()));
    const bool ready = viewer_.document() && viewer_.fully_rendered() && !render_.busy() &&
                       (!options_.select || viewer_.selection_complete()) && !waiting_translation;
    snapshot_stable_frames_ = ready ? snapshot_stable_frames_ + 1 : 0;
    const bool timed_out = glfwGetTime() - start_time_ > kSnapshotTimeoutSeconds;
    const bool failed_open = !loading_ && !viewer_.document() && frame_ > 2;
    if (snapshot_stable_frames_ < 3 && !timed_out && !failed_open) return;

    try {
        auto pix = pdf::Pixmap::create_rgb(fb_width, fb_height);
        std::vector<unsigned char> rows(static_cast<std::size_t>(fb_width) * 3 * static_cast<std::size_t>(fb_height));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, fb_width, fb_height, GL_RGB, GL_UNSIGNED_BYTE, rows.data());
        const std::size_t row_bytes = static_cast<std::size_t>(fb_width) * 3;
        unsigned char* dst = pix->samples();
        for (int y = 0; y < fb_height; ++y) {  // OpenGL đọc từ dưới lên
            std::copy_n(rows.data() + static_cast<std::size_t>(fb_height - 1 - y) * row_bytes, row_bytes,
                        dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(pix->stride()));
        }
        pdf::save_png(*pix, options_.snapshot_path);
        if (options_.select) log::info("SELECTION<<{}>>", viewer_.selection_text());
        if (popup_.visible()) log::info("TRANSLATION<<{}>>", popup_.text());
        log::info("Đã lưu ảnh chụp {} ({}x{})", path_to_utf8(options_.snapshot_path), fb_width, fb_height);
        exit_code_ = (timed_out || failed_open) ? 2 : 0;
        if (timed_out) log::warn("Hết thời gian chờ render, ảnh chụp có thể chưa đủ");
    } catch (const std::exception& e) {
        log::error("Chụp ảnh thất bại: {}", e.what());
        exit_code_ = 3;
    }
}

void App::on_drop(GLFWwindow* window, int count, const char** paths) {
    if (App* app = app_from(window); app != nullptr && count > 0) app->open_file(path_from_utf8(paths[0]));
}

void App::on_refresh(GLFWwindow* window) {
    // Windows chặn vòng lặp chính khi kéo đổi kích thước cửa sổ: vẽ lại ngay tại đây.
    if (App* app = app_from(window)) app->render_frame();
}

void App::on_content_scale(GLFWwindow* window, float xscale, float /*yscale*/) {
    if (App* app = app_from(window)) app->pending_scale_ = std::max(1.0f, xscale);
}

}  // namespace pdfread::app
