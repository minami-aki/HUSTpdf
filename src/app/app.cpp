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

#include "context/translation_plan.hpp"
#include "text/normalize.hpp"
#include "pdf/context.hpp"
#include "pdf/text_page.hpp"
#include "terminology/classifier.hpp"
#include "translate/llama_translator.hpp"
#include "ui/fonts.hpp"
#include "util/log.hpp"
#include "util/paths.hpp"
#include "util/sha256.hpp"
#include "util/timer.hpp"
#include "util/utf8.hpp"

namespace pdfread::app {
namespace {

constexpr double kSnapshotTimeoutSeconds = 60.0;
constexpr double kTranslateDebounceSeconds = 0.25;  // chờ sau khi thả chuột rồi mới dịch
constexpr std::size_t kMaxTranslateChars = 2000;    // giới hạn độ dài đoạn gửi đi dịch
constexpr int kClassifyPages = 3;                    // số trang đầu dùng để phân loại lĩnh vực
constexpr int kExtractMaxPages = 300;                // trích thuật ngữ tối đa bấy nhiêu trang

std::string trim_copy(std::string s) {
    const auto b = s.find_first_not_of(" \t\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\n") - b + 1);
}

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
      viewer_(render_, text_service_, textures_),
      model_window_([] { glfwPostEmptyEvent(); }) {
    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, &App::on_drop);
    glfwSetWindowRefreshCallback(window_, &App::on_refresh);
    glfwSetWindowContentScaleCallback(window_, &App::on_content_scale);
    glfwSetWindowFocusCallback(window_, &App::on_focus);

    float xs = 1.0f;
    float ys = 1.0f;
    glfwGetWindowContentScale(window_, &xs, &ys);
    ui_scale_ = std::max(1.0f, xs);
    ui::apply_style(ui_scale_);

    // Tầng 1: bảng lĩnh vực + glossary (lĩnh vực, người dùng, tài liệu) — nạp trên luồng loader
    // (~40 file CSV, từ điển chuyên ngành ~1 MB) để cửa sổ hiện ngay. Luồng loader chạy tuần tự
    // nên việc này luôn xong trước khi một tài liệu được mở/phân loại.
    loader_.post([this, domains_json = options_.data_dir / "domains.json"] {
        GlossaryLoad r;
        try {
            ScopedTimer t("load glossaries");
            r.catalog = std::make_shared<const terminology::DomainCatalog>(terminology::DomainCatalog::load(domains_json));
            r.store = std::make_unique<terminology::GlossaryStore>(*r.catalog, app_data_dir() / "glossary" / "user.csv");
            (void)r.store->index();  // dựng sẵn chỉ mục
            r.catalog_version = sha256_file(domains_json).value_or("");
            loader_catalog_ = r.catalog;
            loader_catalog_version_ = r.catalog_version;
        } catch (const std::exception& e) {
            r.store.reset();
            r.error = e.what();
        }
        glossary_results_.push(std::move(r));
        glfwPostEmptyEvent();
    });

    // Cơ sở dữ liệu: cài đặt, cache bản dịch, thông tin theo tài liệu.
    try {
        const std::filesystem::path db =
            options_.database_path.empty() ? local_data_dir() / "pdfread.db" : options_.database_path;
        store_ = storage::Store::open(db);
        const auto stats = store_->translation_stats();
        log::info("Cơ sở dữ liệu {}: {} bản dịch trong cache", path_to_utf8(db), stats.entries);
    } catch (const std::exception& e) {
        log::warn("Không mở được cơ sở dữ liệu (chạy không có cache/cài đặt): {}", e.what());
    }
    load_settings();

    glossary_window_.open = options_.open_glossary;
    if (options_.fit_page) viewer_.set_zoom_mode(ui::ZoomMode::FitPage);
    if (options_.zoom) viewer_.set_zoom(*options_.zoom);
    if (!options_.open_path.empty()) open_file(options_.open_path);

    // Nạp mô hình dịch ngay khi mở app (trên luồng suy luận, không chặn UI). Thứ tự:
    // --model, mô hình đã chọn lần trước (cài đặt), file GGUF trong các thư mục mô hình.
    std::filesystem::path model = options_.model_path;
    if (model.empty() && store_) {
        if (const auto saved = store_->get("model.path")) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(path_from_utf8(*saved), ec)) model = path_from_utf8(*saved);
        }
    }
    if (model.empty()) model = find_default_model();
    if (!model.empty()) {
        load_model(model);
    } else {
        log::info("Chưa có mô hình dịch: mở cửa sổ \"Mô hình dịch\" để tải về hoặc chọn file GGUF");
        if (options_.snapshot_path.empty()) model_window_.open = true;  // lần chạy đầu
    }

    start_time_ = glfwGetTime();
}

App::~App() {
    save_reading_position();
    glfwSetWindowUserPointer(window_, nullptr);
    glfwSetDropCallback(window_, nullptr);
    glfwSetWindowRefreshCallback(window_, nullptr);
    glfwSetWindowContentScaleCallback(window_, nullptr);
    glfwSetWindowFocusCallback(window_, nullptr);
    current_ticket_ = 0;  // dừng trích thuật ngữ đang chạy
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
    if (glossary_) glossary_window_.draw(*glossary_, extracted_terms_, extracting_, ui_scale_);
    draw_model_window();
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
            case Kind::Token:
                // Khôi phục placeholder ngay khi stream (giữ lại "⟦F…" chưa trọn).
                if (active_.stream && e.request_id == active_.id) {
                    active_.raw += e.text;
                    popup_.append_text(active_.stream->push(e.text));
                }
                break;
            case Kind::Done:
                if (active_.stream && e.request_id == active_.id) finish_translation(true);
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
            viewer_.clear_translated_highlight();
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

void App::finish_translation(bool from_model) {
    popup_.append_text(active_.stream->flush());
    active_.stream.reset();
    // Bản cuối: khôi phục trên toàn bộ bản dịch thô và hậu kiểm.
    const text::RestoreResult r = text::restore_placeholders(active_.raw, active_.placeholders);
    std::vector<std::string> issues;
    for (const auto& m : r.missing) issues.push_back(std::format("Bản dịch làm mất: {}", m));
    for (const auto& d : r.duplicated) issues.push_back(std::format("Bản dịch lặp lại: {}", d));
    for (const auto& u : r.unknown) issues.push_back(std::format("Ký hiệu lạ trong bản dịch: {}", u));
    if (!active_.original.empty()) {
        for (const auto& n : text::missing_numbers(active_.original, r.text)) issues.push_back(std::format("Bản dịch làm mất số: {}", n));
    }
    for (const auto& t : terminology::check_terms(r.text, active_.terms)) {
        issues.push_back(std::format("Chưa dùng đúng thuật ngữ: {} → {}", t.term_en, t.target()));
    }
    popup_.set_text(text::match_initial_case(trim_copy(r.text), active_.source));
    if (!issues.empty()) log::info("Hậu kiểm: {} vấn đề", issues.size());
    // Chỉ lưu cache bản dịch "sạch": có vấn đề thì lần sau dịch lại (lấy mẫu ngẫu nhiên, có thể tốt hơn).
    if (from_model && issues.empty() && store_ && !active_.cache_key.empty() && !trim_copy(active_.raw).empty()) {
        try {
            store_->put_translation(active_.cache_key, path_to_utf8(model_path_.filename()), active_.source, active_.raw);
        } catch (const std::exception& e) {
            log::warn("Không lưu được cache bản dịch: {}", e.what());
        }
    }
    popup_.set_issues(std::move(issues));
}

void App::start_translation(translate::TranslateRequest request, bool truncated, ui::TranslationPopup::Scope scope,
                            std::vector<ui::TranslationPopup::TermHint> hints, bool bypass_cache) {
    active_.source = request.text;
    if (store_ && options_.use_cache && !model_path_.empty()) {
        active_.cache_key = storage::translation_cache_key(request, path_to_utf8(model_path_.filename()),
                                                           options_.add_generation_prompt);
        std::optional<std::string> hit;
        if (!bypass_cache) {
            try {
                hit = store_->find_translation(active_.cache_key);
            } catch (const std::exception& e) {
                log::warn("Không đọc được cache bản dịch: {}", e.what());
            }
        }
        if (hit) {
            translation_.cancel();
            log::debug("Dùng bản dịch trong cache");
            active_.raw = std::move(*hit);
            active_.stream = std::make_unique<text::PlaceholderStream>(active_.placeholders);
            popup_.show_cached(truncated, scope, std::move(hints));
            finish_translation(false);  // khôi phục placeholder + hậu kiểm như bản dịch mới
            return;
        }
    }
    active_.stream = std::make_unique<text::PlaceholderStream>(active_.placeholders);
    active_.id = translation_.submit(std::move(request));
    popup_.show(active_.id, truncated, scope, std::move(hints));
}

void App::request_translation(bool force_model, bool bypass_cache) {
    if (!viewer_.has_selection()) return;
    viewer_.clear_translated_highlight();
    active_ = ActiveTranslation{};

    // Vùng chọn trong một trang: dùng text đã làm sạch/tách câu của trang (tầng 2) để
    // lập kế hoạch dịch (mở rộng ra trọn câu, ngữ cảnh…).
    if (const auto sel = viewer_.single_page_selection()) {
        const text::PageText& page = sel->text->page_text();
        // Ký tự thuộc vùng chọn: tâm khung ký tự nằm trong một tứ giác tô sáng.
        const text::Span span = page.span_where([&](const text::Box& b) {
            for (const pdf::Quad& q : sel->quads) {
                const float x0 = std::min({q.ul.x, q.ur.x, q.ll.x, q.lr.x}) - 0.5f;
                const float x1 = std::max({q.ul.x, q.ur.x, q.ll.x, q.lr.x}) + 0.5f;
                const float y0 = std::min({q.ul.y, q.ur.y, q.ll.y, q.lr.y}) - 0.5f;
                const float y1 = std::max({q.ul.y, q.ur.y, q.ll.y, q.lr.y}) + 0.5f;
                if (b.cx() >= x0 && b.cx() <= x1 && b.cy() >= y0 && b.cy() <= y1) return true;
            }
            return false;
        });
        const std::shared_ptr<const terminology::GlossaryIndex> index = glossary_ ? glossary_->index() : nullptr;
        context::PlanOptions options;
        options.expand_to_sentence = whole_sentence_;
        options.include_context = options_.include_context;
        options.max_chars = kMaxTranslateChars;
        options.glossary = index.get();
        options.domains = effective_domains();
        if (auto plan = context::make_plan(page, span, options)) {
            // Từ / cụm ngắn có trong glossary: trả lời ngay, không gọi mô hình.
            if (plan->glossary_answer && !force_model) {
                translation_.cancel();
                const auto& a = *plan->glossary_answer;
                ui::TranslationPopup::GlossaryAnswer ans;
                ans.term_en = a.entry.term_en;
                // Viết tắt giữ nguyên khi dịch câu nhưng có nghĩa tiếng Việt để hiển thị (ALARA -> "thấp nhất…").
                ans.term_vi = text::match_initial_case(
                    a.entry.keep_english && !a.entry.term_vi.empty() ? a.entry.term_vi : a.entry.target(), "A");
                ans.canonical = a.entry.alias_of;
                ans.explanation = a.entry.explanation;
                ans.domain = a.entry.domain_id.empty()
                                 ? (a.entry.layer == terminology::Layer::User ? "Glossary của tôi" : "Glossary của tài liệu")
                                 : domain_name(a.entry.domain_id);
                if (a.entry.source == "draft") ans.domain += " · bản nháp";
                if (a.entry.source == "dict") ans.domain += " · từ điển chuyên ngành";
                ans.notes = a.entry.notes;
                for (const auto& alt : a.alternatives) ans.alternatives.emplace_back(alt.target(), domain_name(alt.domain_id));
                log::debug("Tra glossary: {} -> {}", ans.term_en, ans.term_vi);
                popup_.show_glossary(std::move(ans));
                return;
            }
            if (plan->expanded) viewer_.set_translated_highlight(sel->page, page.line_rects(plan->translated));
            using Scope = ui::TranslationPopup::Scope;
            const Scope scope = plan->kind != context::SelectionKind::Partial ? Scope::None
                                : whole_sentence_                               ? Scope::WholeSentence
                                                                                : Scope::SelectionOnly;
            log::debug("Dịch ({}): \"{}\"{}{}", plan->expanded ? "mở rộng ra trọn câu" : "đúng vùng chọn",
                       plan->request.text, plan->terms.empty() ? "" : std::format(" | {} thuật ngữ", plan->terms.size()),
                       plan->request.context.empty() ? std::string() : " | ngữ cảnh: " + plan->request.context);
            std::vector<ui::TranslationPopup::TermHint> hints;
            for (const auto& t : plan->terms) hints.push_back({t.term_en, t.target()});
            active_.placeholders = plan->placeholders;
            active_.terms = plan->terms;
            active_.original = plan->source;
            start_translation(std::move(plan->request), plan->truncated, scope, std::move(hints), bypass_cache);
            return;
        }
    }

    // Vùng chọn qua nhiều trang (hoặc text trang chưa sẵn): dịch nguyên văn đã chọn.
    std::string text = viewer_.selection_text();
    std::replace(text.begin(), text.end(), '\n', ' ');
    const bool truncated = utf8_length(text) > kMaxTranslateChars;
    if (truncated) text = utf8_truncate(text, kMaxTranslateChars);

    translate::TranslateRequest request;
    request.text = std::move(text);
    request.domains = effective_domains();
    active_.original = request.text;
    start_translation(std::move(request), truncated, ui::TranslationPopup::Scope::None, {}, bypass_cache);
}

void App::draw_translation_popup() {
    if (!popup_.visible()) return;
    const auto& anchor = viewer_.selection_screen_rect();
    if (!anchor) return;  // vùng chọn đang ở ngoài vùng nhìn
    using Action = ui::TranslationPopup::Action;
    const Action action = popup_.draw(*anchor, viewer_.viewport_rect(), ui_scale_, translation_.model_state());
    switch (action) {
        case Action::Close:
            translation_.cancel();
            viewer_.clear_translated_highlight();
            break;
        case Action::UseWholeSentence:
        case Action::UseSelectionOnly:
            whole_sentence_ = action == Action::UseWholeSentence;
            if (store_) store_->set_bool("ui.whole_sentence", whole_sentence_);
            request_translation();
            break;
        case Action::TranslateWithModel: request_translation(true); break;
        case Action::Retranslate: request_translation(true, true); break;
        case Action::EditTerm:
            if (glossary_) glossary_window_.edit(*glossary_, popup_.glossary_term());
            break;
        case Action::None: break;
    }
}

// --- lĩnh vực / glossary ----------------------------------------------------------

std::vector<translate::DomainScore> App::effective_domains() const {
    if (!domain_override_.empty()) return {{domain_override_, 1.0f}};
    return auto_domains_;
}

void App::apply_domains() {
    if (glossary_) glossary_->set_domains(effective_domains());
}

std::string App::domain_name(const std::string& id) const {
    if (!catalog_) return id;
    const auto* d = catalog_->find(id);
    return d != nullptr ? d->name_vi : id;
}

void App::draw_domain_combo() {
    const auto domains = effective_domains();
    std::string preview = domains.empty() ? std::string("Lĩnh vực: chưa rõ") : "Lĩnh vực: " + domain_name(domains.front().domain_id);
    if (domain_override_.empty() && domains.size() > 1) preview += std::format(" (+{})", domains.size() - 1);
    ImGui::SetNextItemWidth(std::min(ImGui::CalcTextSize(preview.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 +
                                         ImGui::GetFrameHeight(),
                                     380.0f * ui_scale_));
    if (ImGui::BeginCombo("##domain", preview.c_str(), ImGuiComboFlags_HeightLargest)) {
        std::string auto_label = "Tự động";
        if (auto_domains_.empty()) {
            auto_label += " (chưa xác định được)";
        } else {
            auto_label += ":";
            for (const auto& d : auto_domains_) auto_label += std::format(" {} {:.0f}%,", domain_name(d.domain_id), d.score * 100);
            auto_label.pop_back();
        }
        auto set_override = [&](const std::string& id) {
            domain_override_ = id;
            apply_domains();
            if (store_ && !doc_fingerprint_.empty()) store_->set_domain_override(doc_fingerprint_, id);
        };
        if (ImGui::Selectable(auto_label.c_str(), domain_override_.empty())) set_override({});
        for (const auto& g : catalog_->groups()) {
            ImGui::SeparatorText(g.name_vi.c_str());
            for (std::size_t i : g.subdomains) {
                const auto& s = catalog_->subdomains()[i];
                ImGui::PushID(s.id.c_str());
                if (ImGui::Selectable(s.name_vi.c_str(), domain_override_ == s.id)) set_override(s.id);
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Lĩnh vực của tài liệu: chọn nghĩa chuyên ngành và glossary khi dịch");
}

void App::start_term_extraction(const std::shared_ptr<pdf::Document>& doc, std::uint64_t ticket) {
    extracting_ = true;
    extracted_terms_.clear();
    loader_.post([this, doc, ticket] {
        ScopedTimer t("extract terms");
        std::vector<std::u32string> sentences;
        const int pages = std::min(doc->page_count(), kExtractMaxPages);
        for (int i = 0; i < pages; ++i) {
            if (current_ticket_.load() != ticket) return;  // đã mở tài liệu khác
            try {
                // Không đưa display list vào cache: tránh đẩy các trang đang xem ra khỏi cache.
                const auto tp = pdf::TextPage::build(*doc->display_list_uncached(i), doc->page(i));
                const auto& pt = tp->page_text();
                for (const auto& s : pt.sentences()) sentences.emplace_back(pt.text().substr(s.span.begin, s.span.size()));
            } catch (const std::exception& e) {
                log::debug("Trích thuật ngữ: bỏ qua trang {}: {}", i + 1, e.what());
            }
        }
        extract_results_.push({ticket, terminology::extract_terms(sentences)});
        glfwPostEmptyEvent();
    });
}

void App::choose_model_dialog() {
    nfdu8char_t* out = nullptr;
    const nfdu8filteritem_t filters[] = {{"Mô hình GGUF", "gguf"}};
    const nfdresult_t result = NFD_OpenDialogU8(&out, filters, 1, nullptr);
    if (result == NFD_OKAY) {
        const std::filesystem::path path = path_from_utf8(out);
        NFD_FreePathU8(out);
        if (store_) store_->set("model.path", path_to_utf8(path));
        load_model(path);
    }
}

void App::draw_model_window() {
    ui::ModelWindow::Info info;
    info.model_path = model_path_;
    info.model_status = model_status_;
    info.state = translation_.model_state();
    info.download_dir = options_.model_download_dir;
    info.threads = options_.threads;
    info.default_threads = translate::LlamaTranslator::default_threads();
    if (store_ && model_window_.open) {
        try {
            info.cache = store_->translation_stats();
        } catch (const std::exception&) {
        }
    }
    using Action = ui::ModelWindow::Action;
    switch (model_window_.draw(info, ui_scale_)) {
        case Action::ChooseFile: choose_model_dialog(); break;
        case Action::Downloaded: {
            const std::filesystem::path path = model_window_.downloaded_path();
            if (store_) store_->set("model.path", path_to_utf8(path));
            load_model(path);
            break;
        }
        case Action::ApplyThreads:
            options_.threads = model_window_.chosen_threads();
            if (store_) store_->set_int("model.threads", options_.threads);
            if (!model_path_.empty()) load_model(model_path_);
            break;
        case Action::ClearCache:
            if (store_) store_->clear_translations();
            log::info("Đã xóa cache bản dịch");
            break;
        case Action::None: break;
    }
}

void App::load_settings() {
    if (!store_) return;
    try {
        auto_translate_ = store_->get_bool("ui.auto_translate").value_or(auto_translate_);
        whole_sentence_ = store_->get_bool("ui.whole_sentence").value_or(whole_sentence_);
        if (options_.threads == 0) options_.threads = std::max(0, store_->get_int("model.threads").value_or(0));
    } catch (const std::exception& e) {
        log::warn("Không đọc được cài đặt: {}", e.what());
    }
}

void App::save_reading_position() {
    if (!store_ || doc_fingerprint_.empty() || !viewer_.document()) return;
    try {
        store_->set_last_page(doc_fingerprint_, viewer_.current_page());
    } catch (const std::exception& e) {
        log::warn("Không lưu được trang đang đọc: {}", e.what());
    }
}

void App::load_model(const std::filesystem::path& path) {
    model_path_ = path;
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
    if (ImGui::Checkbox("Dịch tự động", &auto_translate_) && store_) store_->set_bool("ui.auto_translate", auto_translate_);
    ImGui::SetItemTooltip("Dịch ngay khi bôi đen xong. Tắt thì dịch bằng Ctrl+T.");
    ImGui::SameLine();
    if (ImGui::Button("Mô hình…")) model_window_.open = !model_window_.open;
    ImGui::SetItemTooltip("Mô hình dịch: tải về, chọn file GGUF, số luồng, cache bản dịch");
    if (glossary_) {
        ImGui::SameLine(0, 18.0f * ui_scale_);
        draw_domain_combo();
        ImGui::SameLine();
        if (ImGui::Button("Glossary…")) glossary_window_.open = !glossary_window_.open;
        ImGui::SetItemTooltip("Xem, sửa, nhập/xuất glossary (Ctrl+G)");
    }
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
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_G) && glossary_) glossary_window_.open = !glossary_window_.open;
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
    save_reading_position();
    const std::uint64_t ticket = ++open_ticket_;
    current_ticket_ = ticket;
    loading_ = true;
    loading_path_ = path;
    set_status(std::format("Đang mở {}…", file_name_utf8(path)), false);
    loader_.post([this, path, ticket, store = store_.get()] {
        const auto& catalog = loader_catalog_;  // luồng loader: đã nạp ở job đầu tiên
        const std::string& catalog_version = loader_catalog_version_;
        OpenResult r;
        r.ticket = ticket;
        r.path = path;
        try {
            r.doc = pdf::Document::open(path);
        } catch (const std::exception& e) {
            r.error = e.what();
        }
        // Thông tin đã lưu của tài liệu (theo dấu vân tay file): lĩnh vực, trang đang đọc.
        bool classified = false;
        if (r.doc && store != nullptr) {
            try {
                r.fingerprint = storage::document_fingerprint(path);
                if (!r.fingerprint.empty()) {
                    if (const auto info = store->find_document(r.fingerprint)) {
                        r.domain_override = info->domain_override;
                        r.last_page = info->last_page;
                        if (!catalog_version.empty() && info->catalog == catalog_version) {
                            r.domains = info->domains;
                            classified = true;
                            log::debug("Lĩnh vực lấy từ cơ sở dữ liệu");
                        }
                    }
                }
            } catch (const std::exception& e) {
                log::warn("Không đọc được thông tin tài liệu đã lưu: {}", e.what());
            }
        }
        // Tầng 1: phân loại lĩnh vực theo mục lục và vài trang đầu (tiêu đề/tóm tắt nặng hơn).
        if (r.doc && catalog && !classified) {
            try {
                ScopedTimer t("classify domains");
                std::vector<terminology::WeightedText> inputs;
                std::u32string outline;
                for (const std::string& title : r.doc->outline_titles()) outline += to_u32(title) + U". ";
                if (!outline.empty()) inputs.push_back({std::move(outline), 2.0});
                const int pages = std::min(r.doc->page_count(), kClassifyPages);
                for (int i = 0; i < pages; ++i) {
                    const auto tp = pdf::TextPage::build(*r.doc->display_list(i), r.doc->page(i));
                    const auto& pt = tp->page_text();
                    for (std::size_t p = 0; p < pt.paragraphs().size(); ++p) {
                        const auto& sp = pt.paragraphs()[p].span;
                        const double w = (i == 0 && p < 3) ? 3.0 : 1.0;  // tiêu đề, tác giả, tóm tắt
                        inputs.push_back({pt.text().substr(sp.begin, sp.size()), w});
                    }
                }
                r.domains = terminology::classify(inputs, *catalog);
                classified = true;
            } catch (const std::exception& e) {
                log::warn("Phân loại lĩnh vực thất bại: {}", e.what());
            }
        }
        if (r.doc && store != nullptr && !r.fingerprint.empty()) {
            try {
                store->put_document({.fingerprint = r.fingerprint,
                                     .path = path_to_utf8(path),
                                     .catalog = classified ? catalog_version : std::string(),
                                     .domains = r.domains,
                                     .domain_override = r.domain_override,
                                     .last_page = r.last_page});
            } catch (const std::exception& e) {
                log::warn("Không lưu được thông tin tài liệu: {}", e.what());
            }
        }
        open_results_.push(std::move(r));
        glfwPostEmptyEvent();
    });
}

void App::poll_open_results() {
    for (GlossaryLoad& g : glossary_results_.drain()) {
        if (!g.store) {
            log::warn("Không dùng được lĩnh vực/glossary: {}", g.error);
            set_status(std::format("Không nạp được lĩnh vực/glossary: {}", g.error), true);
            continue;
        }
        catalog_ = std::move(g.catalog);
        glossary_ = std::move(g.store);
        catalog_version_ = std::move(g.catalog_version);
        log::info("Đã nạp {} lĩnh vực, {} mục glossary", catalog_->subdomains().size(), glossary_->index()->size());
        for (const std::string& e : glossary_->errors()) log::warn("{}", e);
    }
    for (ExtractResult& r : extract_results_.drain()) {
        if (r.ticket != open_ticket_) continue;
        extracted_terms_ = std::move(r.terms);
        extracting_ = false;
        log::info("Đã trích {} thuật ngữ ứng viên", extracted_terms_.size());
    }
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
        auto_domains_ = std::move(r.domains);
        domain_override_ = r.domain_override;
        if (catalog_ && !domain_override_.empty() && catalog_->find(domain_override_) == nullptr) domain_override_.clear();
        doc_fingerprint_ = r.fingerprint;
        if (glossary_) {
            glossary_->set_document(r.path);
            apply_domains();
            std::string summary;
            for (const auto& d : auto_domains_) summary += std::format(" {} ({:.0f}%)", domain_name(d.domain_id), d.score * 100);
            log::info("Lĩnh vực:{}", summary.empty() ? " chưa xác định" : summary);
            start_term_extraction(r.doc, r.ticket);
        }
        glfwSetWindowTitle(window_, std::format("{} — Pdfread", file_name_utf8(r.path)).c_str());
        bool explicit_page = false;
        if (!initial_view_applied_) {
            initial_view_applied_ = true;
            if (options_.start_page) {
                viewer_.go_to_page(*options_.start_page);
                explicit_page = true;
            }
            if (options_.select) viewer_.select(options_.select->a, options_.select->b, options_.select->unit);
        }
        // Mở lại tài liệu: về trang đang đọc lần trước (không áp dụng khi chụp ảnh kiểm thử).
        if (!explicit_page && r.last_page > 0 && options_.snapshot_path.empty()) {
            viewer_.go_to_page(std::min(r.last_page, viewer_.page_count() - 1));
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
        for (const std::string& issue : popup_.issues()) log::info("ISSUE<<{}>>", issue);
        if (!auto_domains_.empty()) log::info("DOMAIN<<{}>>", auto_domains_.front().domain_id);
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

void App::on_focus(GLFWwindow* window, int focused) {
    // Quay lại app sau khi sửa CSV bằng Excel/trình soạn thảo: đọc lại glossary đã đổi.
    App* app = app_from(window);
    if (app != nullptr && focused && app->glossary_ && app->glossary_->refresh()) {
        log::info("Đã nạp lại glossary đã thay đổi trên đĩa");
    }
}

}  // namespace pdfread::app
